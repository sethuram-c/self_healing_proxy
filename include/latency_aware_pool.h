#ifndef LATENCY_AWARE_POOL_H
#define LATENCY_AWARE_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "backend_health.h"
#include "backend_pool.h"
#include "circuit_breaker.h"
#include "edge_locality.h"
#include "latency_signal.h"

typedef struct {
    uint64_t latency_slack_ns;
} latency_aware_pool_config_t;

typedef struct {
    backend_pool_t *pool;
    backend_health_t *health;
    circuit_breaker_t *breakers;
    edge_locality_t *localities;
    latency_signal_t *latencies;
    size_t count;
    latency_aware_pool_config_t config;
} latency_aware_pool_t;

int latency_aware_pool_init(latency_aware_pool_t *selector, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers,
    edge_locality_t *localities, latency_signal_t *latencies, size_t count,
    const latency_aware_pool_config_t *config);

const backend_target_t *latency_aware_pool_acquire_next(latency_aware_pool_t *selector,
    const edge_locality_t *request_locality, uint64_t now_ns,
    size_t *selected_index, edge_locality_rank_t *selected_rank,
    uint64_t *selected_latency_ns);

#endif
