#define _GNU_SOURCE
#include "ipc_frame.h"
#include "telemetry_channel.h"
#include "trace_context.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>

#define DEFAULT_QUEUE_BYTES (64U * 1024U)

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --socket PATH [--traceparent VALUE] [--attempts N]\n",
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

    if (channel->state == TELEMETRY_CONNECTED) {
        return 0;
    }
    if (channel->state != TELEMETRY_CONNECTING || channel->fd < 0) {
        errno = ENOTCONN;
        return -1;
    }

    pfd.fd = channel->fd;
    pfd.events = POLLOUT | POLLERR | POLLHUP;
    pfd.revents = 0;
    rc = poll(&pfd, 1, 1000);
    if (rc <= 0) {
        if (rc == 0) {
            errno = ETIMEDOUT;
        }
        return -1;
    }
    return telemetry_channel_handle_events(channel, EPOLLOUT);
}

static int drain_queue(telemetry_channel_t *channel)
{
    int loops = 0;
    while (channel->queued_frames > 0 && loops < 100) {
        struct pollfd pfd;
        if (telemetry_channel_flush(channel) < 0) {
            return -1;
        }
        if (channel->queued_frames == 0) {
            return 0;
        }
        pfd.fd = channel->fd;
        pfd.events = POLLOUT | POLLERR | POLLHUP;
        pfd.revents = 0;
        if (poll(&pfd, 1, 20) < 0 && errno != EINTR) {
            return -1;
        }
        loops++;
    }
    return channel->queued_frames == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *socket_path = NULL;
    const char *incoming = NULL;
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
        char trace_hex[TRACE_ID_HEX_LEN + 1U];
        char request_hex[REQUEST_ID_HEX_LEN + 1U];
        char span_hex[SPAN_ID_HEX_LEN + 1U];
        char traceparent[TRACEPARENT_TEXT_LEN + 1U];
        char payload[512];
        int n;

        if (i > 1U && trace_context_next_attempt_span(&ctx) < 0) {
            perror("trace_context_next_attempt_span");
            telemetry_channel_close(&channel);
            return 1;
        }

        if (trace_context_trace_id_hex(&ctx, trace_hex, sizeof(trace_hex)) < 0 ||
            trace_context_request_id_hex(&ctx, request_hex, sizeof(request_hex)) < 0 ||
            trace_context_span_id_hex(&ctx, span_hex, sizeof(span_hex)) < 0 ||
            trace_context_format_traceparent(&ctx, traceparent, sizeof(traceparent)) < 0) {
            telemetry_channel_close(&channel);
            return 1;
        }

        n = snprintf(payload,
                     sizeof(payload),
                     "request_id=%s trace_id=%s span_id=%s attempt=%u source=%s traceparent=%s",
                     request_hex,
                     trace_hex,
                     span_hex,
                     i,
                     traceparent_source_name(source),
                     traceparent);
        if (n < 0 || (size_t)n >= sizeof(payload)) {
            telemetry_channel_close(&channel);
            return 1;
        }

        if (telemetry_channel_enqueue(&channel,
                                      IPC_FRAME_TYPE_EVENT,
                                      payload,
                                      (uint32_t)n) != 0 ||
            telemetry_channel_flush(&channel) < 0) {
            fprintf(stderr, "failed to enqueue correlated telemetry\n");
            telemetry_channel_close(&channel);
            return 1;
        }
    }

    if (drain_queue(&channel) < 0) {
        fprintf(stderr, "failed to drain correlated telemetry\n");
        telemetry_channel_close(&channel);
        return 1;
    }

    {
        char trace_hex[TRACE_ID_HEX_LEN + 1U];
        char request_hex[REQUEST_ID_HEX_LEN + 1U];
        trace_context_trace_id_hex(&ctx, trace_hex, sizeof(trace_hex));
        trace_context_request_id_hex(&ctx, request_hex, sizeof(request_hex));
        printf("PHASE32_CORRELATION_OK\n");
        printf("TRACE_SOURCE=%s\n", traceparent_source_name(source));
        printf("TRACE_ID=%s\n", trace_hex);
        printf("REQUEST_ID=%s\n", request_hex);
        printf("ATTEMPTS=%u\n", attempts);
        printf("SENT=%llu\n", (unsigned long long)channel.sent_frames);
        printf("DROPPED=%llu\n", (unsigned long long)channel.dropped_frames);
    }

    telemetry_channel_close(&channel);
    return 0;
}
