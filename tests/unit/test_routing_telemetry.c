#include "routing_telemetry.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static routing_decision_event_t make_event(void)
{
    routing_decision_event_t event;
    adaptive_score_result_t score = {
        .latency_component = 1000,
        .load_component = 200,
        .combined_score = 466,
        .used_latency = 1,
    };
    memset(&event, 0, sizeof(event));
    assert(routing_decision_event_init(&event, 100, 1, 0, "edge-a",
        EDGE_LOCALITY_SITE, 2000000, 2, 2, 466, 546, &score) == 0);
    return event;
}

static void test_disconnected_drop(void)
{
    telemetry_channel_t channel;
    routing_telemetry_stats_t stats = {0};
    routing_decision_event_t event = make_event();
    telemetry_channel_init(&channel, 4096);
    assert(routing_telemetry_emit(&channel, &event, &stats) == 1);
    assert(stats.dropped == 1);
    assert(stats.emitted == 0);
    assert(channel.dropped_frames == 1);
    telemetry_channel_close(&channel);
}

static void test_connected_enqueue(void)
{
    int fds[2];
    telemetry_channel_t channel;
    routing_telemetry_stats_t stats = {0};
    routing_decision_event_t event = make_event();
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    telemetry_channel_init(&channel, 4096);
    assert(telemetry_channel_adopt_connected_fd(&channel, fds[0]) == 0);
    assert(routing_telemetry_emit(&channel, &event, &stats) == 0);
    assert(stats.emitted == 1);
    assert(channel.queued_frames == 1);
    assert(telemetry_channel_flush(&channel) == 0);
    assert(channel.sent_frames == 1);
    telemetry_channel_close(&channel);
    close(fds[1]);
}

static void test_queue_budget_drops_newest(void)
{
    int fds[2];
    telemetry_channel_t channel;
    routing_telemetry_stats_t stats = {0};
    routing_decision_event_t event = make_event();
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    telemetry_channel_init(&channel, 700);
    assert(telemetry_channel_adopt_connected_fd(&channel, fds[0]) == 0);
    assert(routing_telemetry_emit(&channel, &event, &stats) == 0);
    assert(routing_telemetry_emit(&channel, &event, &stats) == 1);
    assert(stats.emitted == 1);
    assert(stats.dropped == 1);
    assert(channel.queued_frames == 1);
    telemetry_channel_close(&channel);
    close(fds[1]);
}

static void test_invalid_args(void)
{
    routing_telemetry_stats_t stats = {0};
    routing_decision_event_t event = make_event();
    errno = 0;
    assert(routing_telemetry_emit(NULL, &event, &stats) == -1);
    assert(errno == EINVAL);
}

int main(void)
{
    test_disconnected_drop();
    test_connected_enqueue();
    test_queue_budget_drops_newest();
    test_invalid_args();
    puts("routing telemetry export tests passed");
    return 0;
}
