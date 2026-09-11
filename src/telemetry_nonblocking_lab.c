#define _POSIX_C_SOURCE 200809L
#include "ipc_frame.h"
#include "telemetry_channel.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_EVENTS 1000U
#define DEFAULT_PAYLOAD_BYTES 512U
#define DEFAULT_QUEUE_BYTES (64U * 1024U)
#define DEFAULT_RUNTIME_MS 500U
#define DEFAULT_SEND_BUFFER 4096
#define MAX_EVENTS 1000000U
#define MAX_RUNTIME_MS 10000U

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
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
    printf("Usage: %s --socket PATH [options]\n", prog);
    printf("Options:\n");
    printf("  --events N          frames to generate (default %u)\n", DEFAULT_EVENTS);
    printf("  --payload-bytes N   payload bytes per frame (default %u)\n", DEFAULT_PAYLOAD_BYTES);
    printf("  --queue-bytes N     hard queue byte cap (default %u)\n", DEFAULT_QUEUE_BYTES);
    printf("  --runtime-ms N      max epoll drain window (default %u)\n", DEFAULT_RUNTIME_MS);
    printf("  --send-buffer N     SO_SNDBUF bytes (default %d)\n", DEFAULT_SEND_BUFFER);
    printf("  -h, --help          show help\n");
}

static int update_epoll(int epoll_fd, telemetry_channel_t *channel, int *registered)
{
    struct epoll_event ev;
    uint32_t wanted = telemetry_channel_epoll_events(channel);

    if (channel->fd < 0 || wanted == 0) {
        if (*registered) {
            (void)epoll_ctl(epoll_fd, EPOLL_CTL_DEL, channel->fd, NULL);
            *registered = 0;
        }
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

int main(int argc, char **argv)
{
    const char *socket_path = NULL;
    uint32_t event_count = DEFAULT_EVENTS;
    uint32_t payload_bytes = DEFAULT_PAYLOAD_BYTES;
    uint32_t queue_bytes = DEFAULT_QUEUE_BYTES;
    uint32_t runtime_ms = DEFAULT_RUNTIME_MS;
    uint32_t send_buffer = DEFAULT_SEND_BUFFER;
    telemetry_channel_t channel;
    uint8_t *payload = NULL;
    int epoll_fd = -1;
    int registered = 0;
    uint64_t enqueue_start;
    uint64_t enqueue_end;
    uint64_t drain_deadline;
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
        if (strcmp(argv[i], "--socket") == 0) {
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

    if (socket_path == NULL) {
        fprintf(stderr, "--socket is required\n");
        return EXIT_FAILURE;
    }

    telemetry_channel_init(&channel, queue_bytes);

    if (telemetry_channel_connect(&channel, socket_path) < 0) {
        connect_errno = errno;
        printf("TELEMETRY_CONNECT_UNAVAILABLE errno=%d message=%s\n",
               connect_errno,
               strerror(connect_errno));
    } else if (setsockopt(channel.fd, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) < 0) {
        perror("setsockopt SO_SNDBUF");
        telemetry_channel_close(&channel);
        return EXIT_FAILURE;
    }

    payload = malloc(payload_bytes == 0 ? 1U : (size_t)payload_bytes);
    if (payload == NULL) {
        perror("malloc payload");
        telemetry_channel_close(&channel);
        return EXIT_FAILURE;
    }
    for (uint32_t i = 0; i < payload_bytes; ++i) {
        payload[i] = (uint8_t)('A' + (i % 26U));
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

    enqueue_start = monotonic_ms();
    for (uint32_t i = 0; i < event_count; ++i) {
        int rc = telemetry_channel_enqueue(&channel,
                                           IPC_FRAME_TYPE_EVENT,
                                           payload,
                                           payload_bytes);
        if (rc < 0) {
            perror("telemetry_channel_enqueue");
            close(epoll_fd);
            free(payload);
            telemetry_channel_close(&channel);
            return EXIT_FAILURE;
        }
        if (channel.state == TELEMETRY_CONNECTED) {
            (void)telemetry_channel_flush(&channel);
        }
    }
    enqueue_end = monotonic_ms();

    if (channel.fd >= 0 && update_epoll(epoll_fd, &channel, &registered) < 0) {
        perror("epoll_ctl update");
        close(epoll_fd);
        free(payload);
        telemetry_channel_close(&channel);
        return EXIT_FAILURE;
    }

    drain_deadline = monotonic_ms() + runtime_ms;

    while (channel.fd >= 0 && channel.queued_frames > 0 && monotonic_ms() < drain_deadline) {
        struct epoll_event event;
        int ready = epoll_wait(epoll_fd, &event, 1, 20);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }
        if (ready > 0) {
            (void)telemetry_channel_handle_events(&channel, event.events);
        }
        if (channel.fd >= 0 && update_epoll(epoll_fd, &channel, &registered) < 0) {
            perror("epoll_ctl update");
            break;
        }
    }

    printf("PHASE31_NONBLOCKING_OK\n");
    printf("STATE=%s\n", telemetry_channel_state_name(channel.state));
    printf("GENERATED=%u\n", event_count);
    printf("ENQUEUED=%llu\n", (unsigned long long)channel.enqueued_frames);
    printf("SENT=%llu\n", (unsigned long long)channel.sent_frames);
    printf("DROPPED=%llu\n", (unsigned long long)channel.dropped_frames);
    printf("WOULD_BLOCK=%llu\n", (unsigned long long)channel.would_block_count);
    printf("CONNECT_FAILURES=%llu\n", (unsigned long long)channel.connect_failures);
    printf("QUEUED_FRAMES=%zu\n", channel.queued_frames);
    printf("QUEUED_BYTES=%zu\n", channel.queued_bytes);
    printf("ENQUEUE_ELAPSED_MS=%llu\n", (unsigned long long)(enqueue_end - enqueue_start));

    close(epoll_fd);
    free(payload);
    telemetry_channel_close(&channel);
    return EXIT_SUCCESS;
}
