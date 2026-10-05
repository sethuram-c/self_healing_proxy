#define _GNU_SOURCE
#include "routing_telemetry.h"
#include "ipc_frame.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

static routing_decision_event_t event_for(uint64_t seq, const char *backend,
                                          edge_locality_rank_t rank,
                                          int used_latency)
{
    routing_decision_event_t event;
    adaptive_score_result_t score = {
        .latency_component = used_latency ? 1000U : 0U,
        .load_component = 200U,
        .combined_score = used_latency ? 466U : 200U,
        .used_latency = used_latency,
    };
    memset(&event, 0, sizeof(event));
    if (routing_decision_event_init(&event, 1000U + seq, seq, (size_t)(seq - 1U),
        backend, rank, used_latency ? 2000000U : 0U, used_latency ? 2U : 0U,
        2U, score.combined_score, score.combined_score + 80U, &score) != 0) {
        perror("routing_decision_event_init");
        exit(2);
    }
    return event;
}

struct print_ctx { int seen; };
static int print_frame(uint8_t type, const uint8_t *payload, uint32_t len, void *ctxp)
{
    struct print_ctx *ctx = ctxp;
    if (type != IPC_FRAME_TYPE_EVENT) return -1;
    fwrite(payload, 1, len, stdout);
    fputc('\n', stdout);
    ctx->seen++;
    return 0;
}

static int scenario_connected(void)
{
    int fds[2];
    telemetry_channel_t ch;
    routing_telemetry_stats_t stats = {0};
    routing_decision_event_t ev = event_for(1, "edge-a", EDGE_LOCALITY_SITE, 1);
    ipc_frame_decoder_t dec;
    struct print_ctx ctx = {0};
    unsigned char buf[2048];
    ssize_t n;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return 1;
    telemetry_channel_init(&ch, 4096);
    if (telemetry_channel_adopt_connected_fd(&ch, fds[0]) != 0) return 1;
    if (routing_telemetry_emit(&ch, &ev, &stats) != 0 || telemetry_channel_flush(&ch) != 0) return 1;
    n = read(fds[1], buf, sizeof(buf));
    if (n <= 0) return 1;
    ipc_frame_decoder_init(&dec);
    if (ipc_frame_decoder_feed(&dec, buf, (size_t)n, print_frame, &ctx) != 0 || ctx.seen != 1) return 1;
    printf("EMITTED=%llu DROPPED=%llu SENT=%llu\n",
        (unsigned long long)stats.emitted, (unsigned long long)stats.dropped,
        (unsigned long long)ch.sent_frames);
    puts("PHASE55_ROUTING_TELEMETRY_OK");
    telemetry_channel_close(&ch); close(fds[1]); return 0;
}

static int scenario_missing(void)
{
    telemetry_channel_t ch;
    routing_telemetry_stats_t stats = {0};
    routing_decision_event_t ev = event_for(1, "edge-a", EDGE_LOCALITY_SITE, 1);
    telemetry_channel_init(&ch, 4096);
    if (routing_telemetry_emit(&ch, &ev, &stats) != 1) return 1;
    printf("EMITTED=%llu DROPPED=%llu CHANNEL_DROPPED=%llu\n",
        (unsigned long long)stats.emitted, (unsigned long long)stats.dropped,
        (unsigned long long)ch.dropped_frames);
    puts("PHASE55_ROUTING_TELEMETRY_OK");
    telemetry_channel_close(&ch); return 0;
}

static int scenario_bounded(void)
{
    int fds[2]; telemetry_channel_t ch; routing_telemetry_stats_t stats = {0};
    routing_decision_event_t ev = event_for(1, "edge-a", EDGE_LOCALITY_SITE, 1);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return 1;
    telemetry_channel_init(&ch, 700);
    if (telemetry_channel_adopt_connected_fd(&ch, fds[0]) != 0) return 1;
    if (routing_telemetry_emit(&ch, &ev, &stats) != 0) return 1;
    if (routing_telemetry_emit(&ch, &ev, &stats) != 1) return 1;
    printf("QUEUED=%zu EMITTED=%llu DROPPED=%llu\n", ch.queued_frames,
        (unsigned long long)stats.emitted, (unsigned long long)stats.dropped);
    puts("PHASE55_ROUTING_TELEMETRY_OK");
    telemetry_channel_close(&ch); close(fds[1]); return 0;
}

static int wait_connected(telemetry_channel_t *ch)
{
    if (ch->state == TELEMETRY_CONNECTED) return 0;
    struct pollfd pfd = {.fd = ch->fd, .events = POLLOUT};
    if (poll(&pfd, 1, 1000) <= 0) return -1;
    return telemetry_channel_handle_events(ch, EPOLLOUT);
}

static int scenario_bridge(const char *path)
{
    telemetry_channel_t ch; routing_telemetry_stats_t stats = {0};
    telemetry_channel_init(&ch, 8192);
    if (telemetry_channel_connect(&ch, path) != 0 || wait_connected(&ch) != 0) return 1;
    routing_decision_event_t a = event_for(1, "edge-a", EDGE_LOCALITY_SITE, 1);
    routing_decision_event_t b = event_for(2, "zone-b", EDGE_LOCALITY_ZONE, 1);
    routing_decision_event_t c = event_for(3, "remote-c", EDGE_LOCALITY_REMOTE, 0);
    if (routing_telemetry_emit(&ch, &a, &stats) != 0 ||
        routing_telemetry_emit(&ch, &b, &stats) != 0 ||
        routing_telemetry_emit(&ch, &c, &stats) != 0) return 1;
    if (telemetry_channel_flush(&ch) != 0 || ch.queued_frames != 0) return 1;
    printf("EMITTED=%llu DROPPED=%llu SENT=%llu\n",
        (unsigned long long)stats.emitted, (unsigned long long)stats.dropped,
        (unsigned long long)ch.sent_frames);
    puts("PHASE55_ROUTING_TELEMETRY_OK");
    telemetry_channel_close(&ch); return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: routing_telemetry_lab --scenario connected|missing|bounded|bridge [--socket PATH]"); return 0;
    }
    if (argc < 3 || strcmp(argv[1], "--scenario") != 0) return 2;
    if (strcmp(argv[2], "connected") == 0 && argc == 3) return scenario_connected();
    if (strcmp(argv[2], "missing") == 0 && argc == 3) return scenario_missing();
    if (strcmp(argv[2], "bounded") == 0 && argc == 3) return scenario_bounded();
    if (strcmp(argv[2], "bridge") == 0 && argc == 5 && strcmp(argv[3], "--socket") == 0) return scenario_bridge(argv[4]);
    return 2;
}
