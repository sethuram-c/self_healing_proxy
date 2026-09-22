#ifndef HEALTH_AWARE_POOL_H
#define HEALTH_AWARE_POOL_H

#include <stddef.h>

#include "backend_health.h"
#include "backend_pool.h"

typedef struct {
    backend_pool_t *pool;
    backend_health_t *health;
    size_t health_count;
} health_aware_pool_t;

int health_aware_pool_init(
    health_aware_pool_t *selector,
    backend_pool_t *pool,
    backend_health_t *health,
    size_t health_count
);

const backend_target_t *health_aware_pool_acquire_next(
    health_aware_pool_t *selector,
    size_t *selected_index
);

const backend_target_t *health_aware_pool_acquire_next_excluding(
    health_aware_pool_t *selector,
    size_t excluded_index,
    size_t *selected_index
);

size_t health_aware_pool_count_eligible(
    const health_aware_pool_t *selector
);

#endif
