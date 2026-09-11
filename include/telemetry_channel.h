#ifndef TELEMETRY_CHANNEL_H
#define TELEMETRY_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TELEMETRY_DISCONNECTED = 0,
    TELEMETRY_CONNECTING = 1,
    TELEMETRY_CONNECTED = 2
} telemetry_state_t;

typedef struct telemetry_frame_node telemetry_frame_node_t;

typedef struct telemetry_channel {
    int fd;
    telemetry_state_t state;
    size_t max_queue_bytes;
    size_t queued_bytes;
    size_t queued_frames;
    uint64_t enqueued_frames;
    uint64_t sent_frames;
    uint64_t dropped_frames;
    uint64_t would_block_count;
    uint64_t connect_failures;
    telemetry_frame_node_t *head;
    telemetry_frame_node_t *tail;
} telemetry_channel_t;

void telemetry_channel_init(telemetry_channel_t *channel, size_t max_queue_bytes);
void telemetry_channel_close(telemetry_channel_t *channel);

/*
 * Starts a nonblocking AF_UNIX connection. Returns 0 when a socket was created
 * (either already connected or connecting), and -1 when no connection could be
 * started. Failure is intentionally non-fatal to callers; connect_failures is
 * incremented and the channel remains DISCONNECTED.
 */
int telemetry_channel_connect(telemetry_channel_t *channel, const char *path);

/* Adopt an already-connected nonblocking-capable fd. Useful for local wiring/tests. */
int telemetry_channel_adopt_connected_fd(telemetry_channel_t *channel, int fd);

/*
 * Enqueue one Phase-30 frame without blocking.
 *   0 => queued
 *   1 => intentionally dropped by policy
 *  -1 => invalid input / allocation failure
 *
 * When DISCONNECTED, frames are dropped immediately. When the bounded queue
 * would overflow, the newest frame is dropped so previously queued ordering is
 * preserved.
 */
int telemetry_channel_enqueue(telemetry_channel_t *channel,
                              uint8_t type,
                              const void *payload,
                              uint32_t payload_len);

/* Attempt immediate nonblocking progress. EAGAIN is success-with-no-progress. */
int telemetry_channel_flush(telemetry_channel_t *channel);

/* Handle EPOLLOUT/ERR/HUP/RDHUP for the channel fd. */
int telemetry_channel_handle_events(telemetry_channel_t *channel, uint32_t events);

/* Events callers should register for this fd. */
uint32_t telemetry_channel_epoll_events(const telemetry_channel_t *channel);

const char *telemetry_channel_state_name(telemetry_state_t state);

#endif
