#define _GNU_SOURCE
#include "ipc_frame.h"
#include "telemetry_channel.h"
#include "trace_context.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>

#define DEFAULT_QUEUE_BYTES (64U * 1024U)
#define MAX_JSON 32768U
#define MAX_ATTEMPTS 16U

typedef struct {
    uint64_t start_unix_ns;
    uint64_t end_unix_ns;
    uint64_t duration_ns;
    char span_id[SPAN_ID_HEX_LEN + 1U];
    unsigned attempt;
    const char *result;
} attempt_span_t;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --socket PATH [--traceparent VALUE] [--attempts N] "
            "[--backend NAME] [--attempt-ms N]\n",
            prog);
}

static int parse_positive(const char *text, unsigned max_value, unsigned *out)
{
    char *end = NULL;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0UL || value > max_value) {
        return -1;
    }
    *out = (unsigned)value;
    return 0;
}

static uint64_t ts_ns(const struct timespec *ts)
{
    return (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
}

static uint64_t derived_unix_ns(uint64_t anchor_unix_ns, uint64_t anchor_mono_ns, uint64_t mono_ns)
{
    return anchor_unix_ns + (mono_ns - anchor_mono_ns);
}

static int wait_for_connected(telemetry_channel_t *channel)
{
    struct pollfd pfd;
    int rc;
    if (channel->state == TELEMETRY_CONNECTED) return 0;
    if (channel->state != TELEMETRY_CONNECTING || channel->fd < 0) {
        errno = ENOTCONN;
        return -1;
    }
    pfd.fd = channel->fd;
    pfd.events = POLLOUT | POLLERR | POLLHUP;
    pfd.revents = 0;
    rc = poll(&pfd, 1, 1000);
    if (rc <= 0) {
        if (rc == 0) errno = ETIMEDOUT;
        return -1;
    }
    return telemetry_channel_handle_events(channel, EPOLLOUT);
}

static int drain_queue(telemetry_channel_t *channel)
{
    int loops = 0;
    while (channel->queued_frames > 0U && loops < 100) {
        struct pollfd pfd;
        if (telemetry_channel_flush(channel) < 0) return -1;
        if (channel->queued_frames == 0U) return 0;
        pfd.fd = channel->fd;
        pfd.events = POLLOUT | POLLERR | POLLHUP;
        pfd.revents = 0;
        if (poll(&pfd, 1, 20) < 0 && errno != EINTR) return -1;
        loops++;
    }
    return channel->queued_frames == 0U ? 0 : -1;
}

static int appendf(char *buf, size_t cap, size_t *used, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (*used >= cap) {
        errno = ENOSPC;
        return -1;
    }
    va_start(ap, fmt);
    n = vsnprintf(buf + *used, cap - *used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *used) {
        errno = ENOSPC;
        return -1;
    }
    *used += (size_t)n;
    return 0;
}

static int json_safe_text(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (text == NULL || *text == '\0') return 0;
    while (*p != '\0') {
        if (*p < 0x20U || *p == '"' || *p == '\\') return 0;
        ++p;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *socket_path = NULL;
    const char *incoming = NULL;
    const char *backend = "backend-a";
    unsigned attempts = 3U;
    unsigned attempt_ms = 2U;
    trace_context_t ctx;
    traceparent_source_t source;
    telemetry_channel_t channel;
    attempt_span_t spans[MAX_ATTEMPTS];
    char request_span_hex[SPAN_ID_HEX_LEN + 1U];
    char request_id_hex[REQUEST_ID_HEX_LEN + 1U];
    char trace_id_hex[TRACE_ID_HEX_LEN + 1U];
    char remote_parent_hex[SPAN_ID_HEX_LEN + 1U] = "";
    struct timespec rt0, mono0, mono_request_end;
    uint64_t anchor_unix_ns, anchor_mono_ns;
    char payload[MAX_JSON];
    size_t used = 0U;
    unsigned i;

    for (int a = 1; a < argc; ++a) {
        if (strcmp(argv[a], "--socket") == 0 && a + 1 < argc) {
            socket_path = argv[++a];
        } else if (strcmp(argv[a], "--traceparent") == 0 && a + 1 < argc) {
            incoming = argv[++a];
        } else if (strcmp(argv[a], "--attempts") == 0 && a + 1 < argc) {
            if (parse_positive(argv[++a], MAX_ATTEMPTS, &attempts) < 0) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[a], "--backend") == 0 && a + 1 < argc) {
            backend = argv[++a];
        } else if (strcmp(argv[a], "--attempt-ms") == 0 && a + 1 < argc) {
            if (parse_positive(argv[++a], 1000U, &attempt_ms) < 0) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[a], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (socket_path == NULL || !json_safe_text(backend)) {
        usage(argv[0]);
        return 2;
    }

    if (trace_context_start_request(&ctx, incoming, &source) < 0) {
        perror("trace_context_start_request");
        return 1;
    }
    if (trace_context_span_id_hex(&ctx, request_span_hex, sizeof(request_span_hex)) < 0 ||
        trace_context_request_id_hex(&ctx, request_id_hex, sizeof(request_id_hex)) < 0 ||
        trace_context_trace_id_hex(&ctx, trace_id_hex, sizeof(trace_id_hex)) < 0) {
        perror("trace_context format");
        return 1;
    }
    if (ctx.has_remote_parent &&
        trace_context_parent_span_id_hex(&ctx, remote_parent_hex, sizeof(remote_parent_hex)) < 0) {
        perror("trace_context_parent_span_id_hex");
        return 1;
    }

    if (clock_gettime(CLOCK_REALTIME, &rt0) < 0 || clock_gettime(CLOCK_MONOTONIC, &mono0) < 0) {
        perror("clock_gettime");
        return 1;
    }
    anchor_unix_ns = ts_ns(&rt0);
    anchor_mono_ns = ts_ns(&mono0);

    for (i = 0U; i < attempts; ++i) {
        struct timespec start_mono, end_mono;
        struct timespec sleep_for;
        if (trace_context_next_attempt_span(&ctx) < 0 ||
            trace_context_span_id_hex(&ctx, spans[i].span_id, sizeof(spans[i].span_id)) < 0) {
            perror("attempt span id");
            return 1;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &start_mono) < 0) {
            perror("clock_gettime");
            return 1;
        }
        sleep_for.tv_sec = (time_t)(attempt_ms / 1000U);
        sleep_for.tv_nsec = (long)(attempt_ms % 1000U) * 1000000L;
        while (nanosleep(&sleep_for, &sleep_for) < 0 && errno == EINTR) {}
        if (clock_gettime(CLOCK_MONOTONIC, &end_mono) < 0) {
            perror("clock_gettime");
            return 1;
        }
        spans[i].start_unix_ns = derived_unix_ns(anchor_unix_ns, anchor_mono_ns, ts_ns(&start_mono));
        spans[i].end_unix_ns = derived_unix_ns(anchor_unix_ns, anchor_mono_ns, ts_ns(&end_mono));
        spans[i].duration_ns = ts_ns(&end_mono) - ts_ns(&start_mono);
        spans[i].attempt = i + 1U;
        spans[i].result = (i + 1U < attempts) ? "retry" : "success";
    }

    if (clock_gettime(CLOCK_MONOTONIC, &mono_request_end) < 0) {
        perror("clock_gettime");
        return 1;
    }

    if (appendf(payload, sizeof(payload), &used,
                "{\"schema\":\"shp.trace.v1\",\"request_id\":\"%s\",\"trace_id\":\"%s\","
                "\"trace_source\":\"%s\",\"trace_flags\":%u,\"spans\":[",
                request_id_hex, trace_id_hex, traceparent_source_name(source), (unsigned)ctx.trace_flags) < 0)
        return 1;

    if (appendf(payload, sizeof(payload), &used,
                "{\"name\":\"proxy.request\",\"span_id\":\"%s\",\"parent_span_id\":\"%s\","
                "\"kind\":\"server\",\"start_unix_ns\":%llu,\"end_unix_ns\":%llu,"
                "\"duration_ns\":%llu,\"attempt\":0,\"backend\":\"\",\"result\":\"success\"}",
                request_span_hex, remote_parent_hex,
                (unsigned long long)anchor_unix_ns,
                (unsigned long long)derived_unix_ns(anchor_unix_ns, anchor_mono_ns, ts_ns(&mono_request_end)),
                (unsigned long long)(ts_ns(&mono_request_end) - anchor_mono_ns)) < 0)
        return 1;

    for (i = 0U; i < attempts; ++i) {
        if (appendf(payload, sizeof(payload), &used,
                    ",{\"name\":\"proxy.backend_attempt\",\"span_id\":\"%s\","
                    "\"parent_span_id\":\"%s\",\"kind\":\"client\","
                    "\"start_unix_ns\":%llu,\"end_unix_ns\":%llu,\"duration_ns\":%llu,"
                    "\"attempt\":%u,\"backend\":\"%s\",\"result\":\"%s\"}",
                    spans[i].span_id, request_span_hex,
                    (unsigned long long)spans[i].start_unix_ns,
                    (unsigned long long)spans[i].end_unix_ns,
                    (unsigned long long)spans[i].duration_ns,
                    spans[i].attempt, backend, spans[i].result) < 0)
            return 1;
    }
    if (appendf(payload, sizeof(payload), &used, "]}") < 0) return 1;

    telemetry_channel_init(&channel, DEFAULT_QUEUE_BYTES);
    if (telemetry_channel_connect(&channel, socket_path) < 0 || wait_for_connected(&channel) < 0) {
        perror("telemetry connect");
        telemetry_channel_close(&channel);
        return 1;
    }
    if (telemetry_channel_enqueue(&channel, IPC_FRAME_TYPE_EVENT, payload, (uint32_t)used) != 0 ||
        telemetry_channel_flush(&channel) < 0 || drain_queue(&channel) < 0) {
        fprintf(stderr, "failed to deliver trace batch\n");
        telemetry_channel_close(&channel);
        return 1;
    }

    printf("PHASE36_TRACE_BATCH_OK\n");
    printf("TRACE_ID=%s\n", trace_id_hex);
    printf("REQUEST_SPAN_ID=%s\n", request_span_hex);
    printf("SPANS=%u\n", attempts + 1U);
    printf("ATTEMPTS=%u\n", attempts);
    printf("SENT=%llu\n", (unsigned long long)channel.sent_frames);
    printf("DROPPED=%llu\n", (unsigned long long)channel.dropped_frames);
    telemetry_channel_close(&channel);
    return 0;
}
