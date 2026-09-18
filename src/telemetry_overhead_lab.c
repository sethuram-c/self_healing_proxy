#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "ipc_frame.h"
#include "telemetry_channel.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_EVENTS 50000U
#define DEFAULT_PAYLOAD_BYTES 256U
#define DEFAULT_QUEUE_BYTES (64U * 1024U)
#define DEFAULT_RUNTIME_MS 250U
#define DEFAULT_SEND_BUFFER 4096U
#define MAX_EVENTS 2000000U
#define MAX_RUNTIME_MS 10000U

static volatile uint64_t baseline_sink;

static uint64_t clock_ns(clockid_t id)
{
    struct timespec ts;
    if (clock_gettime(id, &ts) < 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static size_t current_rss_bytes(void)
{
    FILE *fp;
    unsigned long total_pages = 0;
    unsigned long resident_pages = 0;
    long page_size;

    fp = fopen("/proc/self/statm", "r");
    if (fp == NULL) {
        return 0;
    }
    if (fscanf(fp, "%lu %lu", &total_pages, &resident_pages) != 2) {
        fclose(fp);
        return 0;
    }
    (void)total_pages;
    fclose(fp);

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 0;
    }
    if (resident_pages > SIZE_MAX / (size_t)page_size) {
        return 0;
    }
    return (size_t)resident_pages * (size_t)page_size;
}

static int parse_u32(const char *text, uint32_t min, uint32_t max, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || out == NULL || text[0] == '\0') {
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static void usage(const char *prog)
{
    printf("Usage: %s --mode disabled|telemetry [options]\n", prog);
    printf("Options:\n");
    printf("  --socket PATH        AF_UNIX collector path for telemetry mode\n");
    printf("  --events N           generated events (default %u)\n", DEFAULT_EVENTS);
    printf("  --payload-bytes N    payload bytes per event (default %u)\n", DEFAULT_PAYLOAD_BYTES);
    printf("  --queue-bytes N      telemetry queue cap (default %u)\n", DEFAULT_QUEUE_BYTES);
    printf("  --runtime-ms N       post-producer drain window (default %u)\n", DEFAULT_RUNTIME_MS);
    printf("  --send-buffer N      SO_SNDBUF bytes (default %u)\n", DEFAULT_SEND_BUFFER);
    printf("  -h, --help           show help\n");
}

static int update_epoll(int epoll_fd, telemetry_channel_t *channel, int *registered)
{
    struct epoll_event ev;
    uint32_t wanted = telemetry_channel_epoll_events(channel);

    if (channel->fd < 0 || wanted == 0) {
        if (*registered && channel->fd >= 0) {
            (void)epoll_ctl(epoll_fd, EPOLL_CTL_DEL, channel->fd, NULL);
        }
        *registered = 0;
        return 0;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = wanted;
    ev.data.ptr = channel;

    if (!*registered) {
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, channel->fd, &ev) < 0) {
            return -1;
        }
        *registered = 1;
    } else if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, channel->fd, &ev) < 0) {
        return -1;
    }
    return 0;
}

static void baseline_work(const uint8_t *payload, uint32_t payload_bytes, uint32_t sequence)
{
    uint64_t value = sequence;
    if (payload_bytes > 0) {
        value ^= payload[sequence % payload_bytes];
        value = (value << 5) ^ payload[(sequence * 17U) % payload_bytes];
    }
    baseline_sink ^= value;
}

int main(int argc, char **argv)
{
    const char *mode = NULL;
    const char *socket_path = NULL;
    uint32_t event_count = DEFAULT_EVENTS;
    uint32_t payload_bytes = DEFAULT_PAYLOAD_BYTES;
    uint32_t queue_bytes = DEFAULT_QUEUE_BYTES;
    uint32_t runtime_ms = DEFAULT_RUNTIME_MS;
    uint32_t send_buffer = DEFAULT_SEND_BUFFER;
    telemetry_channel_t channel;
    uint8_t *payload = NULL;
    int telemetry_enabled = 0;
    int epoll_fd = -1;
    int registered = 0;
    uint64_t wall_start;
    uint64_t wall_end;
    uint64_t cpu_start;
    uint64_t cpu_end;
    uint64_t total_start;
    uint64_t total_end;
    uint64_t drain_deadline;
    size_t rss_before;
    size_t rss_after_producer;
    size_t peak_queue_bytes = 0;
    size_t peak_queue_frames = 0;
    int connect_errno = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "missing value for %s\n", argv[i]);
            return EXIT_FAILURE;
        }
        if (strcmp(argv[i], "--mode") == 0) {
            mode = argv[++i];
        } else if (strcmp(argv[i], "--socket") == 0) {
            socket_path = argv[++i];
        } else if (strcmp(argv[i], "--events") == 0) {
            if (parse_u32(argv[++i], 1, MAX_EVENTS, &event_count) < 0) {
                fprintf(stderr, "invalid --events\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--payload-bytes") == 0) {
            if (parse_u32(argv[++i], 0, IPC_FRAME_MAX_PAYLOAD, &payload_bytes) < 0) {
                fprintf(stderr, "invalid --payload-bytes\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--queue-bytes") == 0) {
            if (parse_u32(argv[++i], IPC_FRAME_HEADER_SIZE, 16U * 1024U * 1024U, &queue_bytes) < 0) {
                fprintf(stderr, "invalid --queue-bytes\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--runtime-ms") == 0) {
            if (parse_u32(argv[++i], 1, MAX_RUNTIME_MS, &runtime_ms) < 0) {
                fprintf(stderr, "invalid --runtime-ms\n");
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--send-buffer") == 0) {
            if (parse_u32(argv[++i], 512, 16U * 1024U * 1024U, &send_buffer) < 0) {
                fprintf(stderr, "invalid --send-buffer\n");
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (mode == NULL || (strcmp(mode, "disabled") != 0 && strcmp(mode, "telemetry") != 0)) {
        fprintf(stderr, "--mode must be disabled or telemetry\n");
        return EXIT_FAILURE;
    }
    telemetry_enabled = strcmp(mode, "telemetry") == 0;
    if (telemetry_enabled && socket_path == NULL) {
        fprintf(stderr, "--socket is required in telemetry mode\n");
        return EXIT_FAILURE;
    }

    payload = malloc(payload_bytes == 0 ? 1U : (size_t)payload_bytes);
    if (payload == NULL) {
        perror("malloc payload");
        return EXIT_FAILURE;
    }
    for (uint32_t i = 0; i < payload_bytes; ++i) {
        payload[i] = (uint8_t)('A' + (i % 26U));
    }

    telemetry_channel_init(&channel, queue_bytes);
    if (telemetry_enabled) {
        if (telemetry_channel_connect(&channel, socket_path) < 0) {
            connect_errno = errno;
        } else {
            int sndbuf = (int)send_buffer;
            if (setsockopt(channel.fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
                perror("setsockopt SO_SNDBUF");
                free(payload);
                telemetry_channel_close(&channel);
                return EXIT_FAILURE;
            }
        }

        epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd < 0) {
            perror("epoll_create1");
            free(payload);
            telemetry_channel_close(&channel);
            return EXIT_FAILURE;
        }
        if (channel.fd >= 0 && update_epoll(epoll_fd, &channel, &registered) < 0) {
            perror("epoll_ctl");
            close(epoll_fd);
            free(payload);
            telemetry_channel_close(&channel);
            return EXIT_FAILURE;
        }
    }

    rss_before = current_rss_bytes();
    total_start = clock_ns(CLOCK_MONOTONIC);
    wall_start = clock_ns(CLOCK_MONOTONIC);
    cpu_start = clock_ns(CLOCK_PROCESS_CPUTIME_ID);

    for (uint32_t i = 0; i < event_count; ++i) {
        baseline_work(payload, payload_bytes, i);
        if (telemetry_enabled) {
            int rc = telemetry_channel_enqueue(&channel,
                                               IPC_FRAME_TYPE_EVENT,
                                               payload,
                                               payload_bytes);
            if (rc < 0) {
                perror("telemetry_channel_enqueue");
                if (epoll_fd >= 0) {
                    close(epoll_fd);
                }
                free(payload);
                telemetry_channel_close(&channel);
                return EXIT_FAILURE;
            }
            if (channel.state == TELEMETRY_CONNECTED) {
                (void)telemetry_channel_flush(&channel);
            }
            if (channel.queued_bytes > peak_queue_bytes) {
                peak_queue_bytes = channel.queued_bytes;
            }
            if (channel.queued_frames > peak_queue_frames) {
                peak_queue_frames = channel.queued_frames;
            }
        }
    }

    cpu_end = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    wall_end = clock_ns(CLOCK_MONOTONIC);
    rss_after_producer = current_rss_bytes();

    if (telemetry_enabled && channel.fd >= 0) {
        if (update_epoll(epoll_fd, &channel, &registered) < 0) {
            perror("epoll_ctl update");
        }
        drain_deadline = clock_ns(CLOCK_MONOTONIC) + (uint64_t)runtime_ms * 1000000ULL;
        while (channel.fd >= 0 && channel.queued_frames > 0 && clock_ns(CLOCK_MONOTONIC) < drain_deadline) {
            struct epoll_event event;
            int ready = epoll_wait(epoll_fd, &event, 1, 10);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (ready > 0) {
                (void)telemetry_channel_handle_events(&channel, event.events);
            }
            if (channel.fd >= 0 && update_epoll(epoll_fd, &channel, &registered) < 0) {
                break;
            }
        }
    }

    total_end = clock_ns(CLOCK_MONOTONIC);

    printf("PHASE38_OVERHEAD_SAMPLE_OK\n");
    printf("MODE=%s\n", mode);
    printf("EVENTS=%u\n", event_count);
    printf("PAYLOAD_BYTES=%u\n", payload_bytes);
    printf("QUEUE_LIMIT_BYTES=%u\n", queue_bytes);
    printf("STATE=%s\n", telemetry_channel_state_name(channel.state));
    printf("CONNECT_ERRNO=%d\n", connect_errno);
    printf("CONNECT_FAILURES=%llu\n", (unsigned long long)channel.connect_failures);
    printf("ENQUEUED=%llu\n", (unsigned long long)channel.enqueued_frames);
    printf("SENT=%llu\n", (unsigned long long)channel.sent_frames);
    printf("DROPPED=%llu\n", (unsigned long long)channel.dropped_frames);
    printf("WOULD_BLOCK=%llu\n", (unsigned long long)channel.would_block_count);
    printf("QUEUED_FRAMES=%zu\n", channel.queued_frames);
    printf("QUEUED_BYTES=%zu\n", channel.queued_bytes);
    printf("PEAK_QUEUE_FRAMES=%zu\n", peak_queue_frames);
    printf("PEAK_QUEUE_BYTES=%zu\n", peak_queue_bytes);
    printf("PRODUCER_WALL_NS=%llu\n", (unsigned long long)(wall_end - wall_start));
    printf("PRODUCER_CPU_NS=%llu\n", (unsigned long long)(cpu_end - cpu_start));
    printf("WALL_NS_PER_EVENT=%llu\n", (unsigned long long)((wall_end - wall_start) / event_count));
    printf("CPU_NS_PER_EVENT=%llu\n", (unsigned long long)((cpu_end - cpu_start) / event_count));
    printf("TOTAL_ELAPSED_NS=%llu\n", (unsigned long long)(total_end - total_start));
    printf("RSS_BEFORE_BYTES=%zu\n", rss_before);
    printf("RSS_AFTER_PRODUCER_BYTES=%zu\n", rss_after_producer);
    printf("RSS_DELTA_BYTES=%zu\n", rss_after_producer > rss_before ? rss_after_producer - rss_before : 0U);
    printf("BASELINE_SINK=%llu\n", (unsigned long long)baseline_sink);

    if (epoll_fd >= 0) {
        close(epoll_fd);
    }
    free(payload);
    telemetry_channel_close(&channel);
    return EXIT_SUCCESS;
}
