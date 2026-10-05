#include "routing_telemetry.h"

#include <errno.h>
#include <string.h>

int routing_telemetry_emit(telemetry_channel_t *channel,
                           const routing_decision_event_t *event,
                           routing_telemetry_stats_t *stats)
{
    char json[ROUTING_TELEMETRY_JSON_MAX];
    int rc;

    if (channel == NULL || event == NULL || stats == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (routing_decision_serialize_json(event, json, sizeof(json)) != 0) {
        stats->serialize_errors++;
        return -1;
    }

    rc = telemetry_channel_enqueue(channel,
                                   ROUTING_TELEMETRY_FRAME_TYPE,
                                   json,
                                   (uint32_t)strlen(json));
    if (rc == 0) {
        stats->emitted++;
        return 0;
    }
    if (rc == 1) {
        stats->dropped++;
        return 1;
    }

    stats->enqueue_errors++;
    return -1;
}
