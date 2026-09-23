#ifndef CIRCUIT_AWARE_POOL_H
#define CIRCUIT_AWARE_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "backend_health.h"
#include "backend_pool.h"
#include "circuit_breaker.h"

typedef struct {
    backend_pool_t *pool;
    backend_health_t *health;
    circuit_breaker_t *breakers;
    size_t count;
} circuit_aware_pool_t;

int circuit_aware_pool_init(circuit_aware_pool_t *selector, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers, size_t count);
const backend_target_t *circuit_aware_pool_acquire_next(circuit_aware_pool_t *selector,
    uint64_t now_ns, size_t *selected_index);
const backend_target_t *circuit_aware_pool_acquire_next_excluding(circuit_aware_pool_t *selector,
    uint64_t now_ns, size_t excluded_index, size_t *selected_index);

#endif
