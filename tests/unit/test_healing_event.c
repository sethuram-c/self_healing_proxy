#include "healing_event.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char out[HEALING_EVENT_JSON_MAX];
    char tiny[32];
    healing_event_t event = {
        123U, 7U, "backend-\"a\n", "recovery_complete", "HEALTHY",
        "CLOSED", "READY", "ROUTE_ENABLED", "ok\\done"
    };

    assert(healing_event_serialize(&event, out, sizeof(out)) == 0);
    assert(strstr(out, "\"schema\":\"shp.healing.v1\"") != NULL);
    assert(strstr(out, "\"timestamp_ns\":123") != NULL);
    assert(strstr(out, "\"sequence\":7") != NULL);
    assert(strstr(out, "backend-\\\"a\\n") != NULL);
    assert(strstr(out, "ok\\\\done") != NULL);
    errno = 0;
    assert(healing_event_serialize(&event, tiny, sizeof(tiny)) == -1);
    assert(errno == ENOSPC);
    errno = 0;
    assert(healing_event_serialize(NULL, out, sizeof(out)) == -1);
    assert(errno == EINVAL);
    puts("healing event tests passed");
    return 0;
}
