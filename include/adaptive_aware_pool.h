#ifndef ADAPTIVE_AWARE_POOL_H
#define ADAPTIVE_AWARE_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "adaptive_score.h"
#include "backend_health.h"
#include "backend_pool.h"
#include "circuit_breaker.h"
#include "edge_locality.h"
#include "latency_signal.h"

typedef struct {
    backend_pool_t *pool;
    backend_health_t *health;
    circuit_breaker_t *breakers;
    edge_locality_t *localities;
    latency_signal_t *latencies;
    size_t count;
    adaptive_score_config_t score_config;
} adaptive_aware_pool_t;

typedef struct {
    size_t selected_index;
    edge_locality_rank_t selected_rank;
    uint64_t fastest_fresh_latency_ns;
    size_t fresh_latency_candidates;
    size_t safe_locality_candidates;
    uint32_t best_score;
    uint32_t score_threshold;
    adaptive_score_result_t selected_score;
} adaptive_selection_trace_t;

int adaptive_aware_pool_init(adaptive_aware_pool_t *selector, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers,
    edge_locality_t *localities, latency_signal_t *latencies, size_t count,
    const adaptive_score_config_t *score_config);

const backend_target_t *adaptive_aware_pool_acquire_next(adaptive_aware_pool_t *selector,
    const edge_locality_t *request_locality, uint64_t now_ns,
    size_t *selected_index, edge_locality_rank_t *selected_rank,
    adaptive_score_result_t *selected_score);

const backend_target_t *adaptive_aware_pool_acquire_next_traced(adaptive_aware_pool_t *selector,
    const edge_locality_t *request_locality, uint64_t now_ns,
    adaptive_selection_trace_t *trace);

#endif
