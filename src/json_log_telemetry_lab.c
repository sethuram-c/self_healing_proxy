#define _GNU_SOURCE
#include "ipc_frame.h"
#include "json_log.h"
#include "telemetry_channel.h"
#include "trace_context.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>

#define DEFAULT_QUEUE_BYTES (64U * 1024U)

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --socket PATH [--traceparent VALUE] [--attempts N] "
            "[--backend NAME] [--message TEXT]\n",
            prog);
}

static int parse_positive(const char *text, unsigned *out)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0UL || value > 1000UL) {
        return -1;
    }
    *out = (unsigned)value;
    return 0;
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

int main(int argc, char **argv)
{
    const char *socket_path = NULL;
    const char *incoming = NULL;
    const char *backend = "backend-a";
    const char *message = "attempt completed";
    unsigned attempts = 3U;
    trace_context_t ctx;
    traceparent_source_t source;
    telemetry_channel_t channel;
    unsigned i;

    for (int iarg = 1; iarg < argc; ++iarg) {
        if (strcmp(argv[iarg], "--socket") == 0 && iarg + 1 < argc) {
            socket_path = argv[++iarg];
        } else if (strcmp(argv[iarg], "--traceparent") == 0 && iarg + 1 < argc) {
            incoming = argv[++iarg];
        } else if (strcmp(argv[iarg], "--attempts") == 0 && iarg + 1 < argc) {
            if (parse_positive(argv[++iarg], &attempts) < 0) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[iarg], "--backend") == 0 && iarg + 1 < argc) {
            backend = argv[++iarg];
        } else if (strcmp(argv[iarg], "--message") == 0 && iarg + 1 < argc) {
            message = argv[++iarg];
        } else if (strcmp(argv[iarg], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (socket_path == NULL) {
        usage(argv[0]);
        return 2;
    }

    if (trace_context_start_request(&ctx, incoming, &source) < 0) {
        perror("trace_context_start_request");
        return 1;
    }

    telemetry_channel_init(&channel, DEFAULT_QUEUE_BYTES);
    if (telemetry_channel_connect(&channel, socket_path) < 0 || wait_for_connected(&channel) < 0) {
        perror("telemetry connect");
        telemetry_channel_close(&channel);
        return 1;
    }

    for (i = 1U; i <= attempts; ++i) {
        char payload[JSON_LOG_MAX_LINE];
        json_log_event_t event;
        const char *result;
        int payload_len;

        if (i > 1U && trace_context_next_attempt_span(&ctx) < 0) {
            perror("trace_context_next_attempt_span");
            telemetry_channel_close(&channel);
            return 1;
        }

        result = i < attempts ? "retry" : "success";
        event.timestamp_unix_ns = json_log_now_unix_ns();
        event.event = "backend_attempt";
        event.backend = backend;
        event.result = result;
        event.message = message;
        event.attempt = i;
        event.trace_source = source;
        event.trace = &ctx;

        payload_len = json_log_serialize(&event, payload, sizeof(payload));
        if (payload_len < 0) {
            perror("json_log_serialize");
            telemetry_channel_close(&channel);
            return 1;
        }

        if (telemetry_channel_enqueue(&channel,
                                      IPC_FRAME_TYPE_EVENT,
                                      payload,
                                      (uint32_t)payload_len) != 0 ||
            telemetry_channel_flush(&channel) < 0) {
            fprintf(stderr, "failed to enqueue JSON telemetry\n");
            telemetry_channel_close(&channel);
            return 1;
        }
    }

    if (drain_queue(&channel) < 0) {
        fprintf(stderr, "failed to drain JSON telemetry\n");
        telemetry_channel_close(&channel);
        return 1;
    }

    printf("PHASE33_JSON_LOGGING_OK\n");
    printf("EVENTS=%u\n", attempts);
    printf("SENT=%llu\n", (unsigned long long)channel.sent_frames);
    printf("DROPPED=%llu\n", (unsigned long long)channel.dropped_frames);
    telemetry_channel_close(&channel);
    return 0;
}
