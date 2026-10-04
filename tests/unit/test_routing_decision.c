#include "routing_decision.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    adaptive_score_result_t score = {1000U, 200U, 466U, 1};
    routing_decision_event_t e;
    assert(routing_decision_event_init(&e, 42U, 7U, 1U, "edge-\"b\\x\n",
        EDGE_LOCALITY_ZONE, 2000000U, 2U, 3U, 450U, 530U, &score) == 0);
    assert(e.fallback_reason == ROUTING_FALLBACK_SITE_UNAVAILABLE);
    char buf[1024];
    assert(routing_decision_serialize_json(&e, buf, sizeof(buf)) == 0);
    assert(strstr(buf, "\"schema\":\"shp.routing.v1\"") != NULL);
    assert(strstr(buf, "\"locality_rank\":\"ZONE\"") != NULL);
    assert(strstr(buf, "\"fallback_reason\":\"SITE_UNAVAILABLE\"") != NULL);
    assert(strstr(buf, "edge-\\\"b\\\\x\\n") != NULL);
    assert(strstr(buf, "\"used_latency\":true") != NULL);

    char tiny[16];
    errno = 0;
    assert(routing_decision_serialize_json(&e, tiny, sizeof(tiny)) == -1);
    assert(errno == ENOSPC);

    assert(strcmp(routing_fallback_reason_name(ROUTING_FALLBACK_LOCAL_UNAVAILABLE),
        "LOCAL_UNAVAILABLE") == 0);
    puts("routing decision observability tests passed");
    return 0;
}
