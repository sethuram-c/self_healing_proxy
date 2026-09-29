#ifndef EDGE_AWARE_POOL_H
#define EDGE_AWARE_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "backend_health.h"
#include "backend_pool.h"
#include "circuit_breaker.h"
#include "edge_locality.h"

typedef struct {
    backend_pool_t *pool;
    backend_health_t *health;
    circuit_breaker_t *breakers;
    edge_locality_t *localities;
    size_t count;
} edge_aware_pool_t;

int edge_aware_pool_init(edge_aware_pool_t *selector, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers,
    edge_locality_t *localities, size_t count);

const backend_target_t *edge_aware_pool_acquire_next(edge_aware_pool_t *selector,
    const edge_locality_t *request_locality, uint64_t now_ns,
    size_t *selected_index, edge_locality_rank_t *selected_rank);

#endif
