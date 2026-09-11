#define _GNU_SOURCE
#include "ipc_frame.h"
#include "telemetry_channel.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_disconnected_drops_immediately(void)
{
    telemetry_channel_t channel;
    telemetry_channel_init(&channel, 4096);

    assert(telemetry_channel_enqueue(&channel,
                                     IPC_FRAME_TYPE_EVENT,
                                     "x",
                                     1) == 1);
    assert(channel.dropped_frames == 1);
    assert(channel.queued_frames == 0);
    telemetry_channel_close(&channel);
}

static void test_connected_flushes_encoded_frame(void)
{
    int pair[2];
    telemetry_channel_t channel;
    uint8_t buf[64];
    ssize_t n;

    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    telemetry_channel_init(&channel, 4096);
    assert(telemetry_channel_adopt_connected_fd(&channel, pair[0]) == 0);

    assert(telemetry_channel_enqueue(&channel,
                                     IPC_FRAME_TYPE_EVENT,
                                     "abc",
                                     3) == 0);
    assert(telemetry_channel_flush(&channel) == 0);
    assert(channel.sent_frames == 1);
    assert(channel.queued_frames == 0);

    n = recv(pair[1], buf, sizeof(buf), 0);
    assert(n == (ssize_t)(IPC_FRAME_HEADER_SIZE + 3));
    assert(memcmp(buf, "SHP1", 4) == 0);
    assert(buf[5] == IPC_FRAME_TYPE_EVENT);
    assert(memcmp(buf + IPC_FRAME_HEADER_SIZE, "abc", 3) == 0);

    close(pair[1]);
    telemetry_channel_close(&channel);
}

static void test_queue_cap_drops_newest(void)
{
    int pair[2];
    telemetry_channel_t channel;
    uint8_t payload[100];
    size_t one_frame = IPC_FRAME_HEADER_SIZE + sizeof(payload);

    memset(payload, 'Q', sizeof(payload));
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    telemetry_channel_init(&channel, one_frame * 2);
    assert(telemetry_channel_adopt_connected_fd(&channel, pair[0]) == 0);

    assert(telemetry_channel_enqueue(&channel,
                                     IPC_FRAME_TYPE_EVENT,
                                     payload,
                                     sizeof(payload)) == 0);
    assert(telemetry_channel_enqueue(&channel,
                                     IPC_FRAME_TYPE_EVENT,
                                     payload,
                                     sizeof(payload)) == 0);
    assert(telemetry_channel_enqueue(&channel,
                                     IPC_FRAME_TYPE_EVENT,
                                     payload,
                                     sizeof(payload)) == 1);

    assert(channel.queued_frames == 2);
    assert(channel.dropped_frames == 1);
    assert(channel.queued_bytes == one_frame * 2);

    close(pair[1]);
    telemetry_channel_close(&channel);
}

static void test_peer_close_never_blocks_or_crashes(void)
{
    int pair[2];
    telemetry_channel_t channel;

    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) == 0);
    telemetry_channel_init(&channel, 4096);
    assert(telemetry_channel_adopt_connected_fd(&channel, pair[0]) == 0);
    close(pair[1]);

    assert(telemetry_channel_enqueue(&channel,
                                     IPC_FRAME_TYPE_EVENT,
                                     "dead-peer",
                                     9) == 0);

    errno = 0;
    assert(telemetry_channel_flush(&channel) == -1);
    assert(channel.state == TELEMETRY_DISCONNECTED);
    assert(channel.fd == -1);
    assert(channel.queued_frames == 0);
    assert(channel.dropped_frames == 1);

    telemetry_channel_close(&channel);
}

int main(void)
{
    test_disconnected_drops_immediately();
    test_connected_flushes_encoded_frame();
    test_queue_cap_drops_newest();
    test_peer_close_never_blocks_or_crashes();
    puts("telemetry channel tests passed");
    return 0;
}
