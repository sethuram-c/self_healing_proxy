#ifndef ROUTING_TELEMETRY_H
#define ROUTING_TELEMETRY_H

#include <stdint.h>

#include "routing_decision.h"
#include "telemetry_channel.h"

#define ROUTING_TELEMETRY_FRAME_TYPE 1U
#define ROUTING_TELEMETRY_JSON_MAX 1024U

typedef struct {
    uint64_t emitted;
    uint64_t dropped;
    uint64_t serialize_errors;
    uint64_t enqueue_errors;
} routing_telemetry_stats_t;

/*
 * Serialize one shp.routing.v1 event and enqueue it on the existing bounded,
 * nonblocking telemetry channel.
 *   0 => enqueued
 *   1 => intentionally dropped by telemetry policy
 *  -1 => serialization/enqueue error
 */
int routing_telemetry_emit(telemetry_channel_t *channel,
                           const routing_decision_event_t *event,
                           routing_telemetry_stats_t *stats);

#endif
