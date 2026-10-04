#ifndef ROUTING_DECISION_H
#define ROUTING_DECISION_H

#include <stddef.h>
#include <stdint.h>

#include "adaptive_score.h"
#include "edge_locality.h"

#define ROUTING_DECISION_SCHEMA "shp.routing.v1"
#define ROUTING_DECISION_BACKEND_MAX 64U

typedef enum {
    ROUTING_FALLBACK_NONE = 0,
    ROUTING_FALLBACK_SITE_UNAVAILABLE,
    ROUTING_FALLBACK_SITE_ZONE_UNAVAILABLE,
    ROUTING_FALLBACK_LOCAL_UNAVAILABLE
} routing_fallback_reason_t;

typedef struct {
    uint64_t timestamp_ns;
    uint64_t sequence;
    size_t selected_index;
    char backend[ROUTING_DECISION_BACKEND_MAX];
    edge_locality_rank_t selected_rank;
    routing_fallback_reason_t fallback_reason;
    uint64_t fastest_fresh_latency_ns;
    size_t fresh_latency_candidates;
    size_t safe_locality_candidates;
    uint32_t best_score;
    uint32_t score_threshold;
    adaptive_score_result_t selected_score;
} routing_decision_event_t;

const char *routing_fallback_reason_name(routing_fallback_reason_t reason);
routing_fallback_reason_t routing_fallback_reason_for_rank(edge_locality_rank_t rank);
int routing_decision_event_init(routing_decision_event_t *event,
    uint64_t timestamp_ns, uint64_t sequence, size_t selected_index,
    const char *backend, edge_locality_rank_t selected_rank,
    uint64_t fastest_fresh_latency_ns, size_t fresh_latency_candidates,
    size_t safe_locality_candidates, uint32_t best_score,
    uint32_t score_threshold, const adaptive_score_result_t *selected_score);
int routing_decision_serialize_json(const routing_decision_event_t *event,
    char *buffer, size_t buffer_size);

#endif
