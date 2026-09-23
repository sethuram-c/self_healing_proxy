#include "circuit_aware_pool.h"

#include <stdint.h>

int circuit_aware_pool_init(circuit_aware_pool_t *selector, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers, size_t count)
{
    if (selector == NULL || pool == NULL || pool->targets == NULL || pool->count == 0
        || health == NULL || breakers == NULL || count != pool->count) {
        return -1;
    }
    selector->pool = pool;
    selector->health = health;
    selector->breakers = breakers;
    selector->count = count;
    return 0;
}

const backend_target_t *circuit_aware_pool_acquire_next_excluding(circuit_aware_pool_t *selector,
    uint64_t now_ns, size_t excluded_index, size_t *selected_index)
{
    if (selector == NULL || selector->pool == NULL || selector->health == NULL
        || selector->breakers == NULL || selector->count != selector->pool->count) {
        return NULL;
    }
    backend_pool_t *pool = selector->pool;
    for (size_t offset = 0; offset < pool->count; ++offset) {
        size_t index = (pool->next_index + offset) % pool->count;
        backend_target_t *target = &pool->targets[index];
        if (index == excluded_index || !backend_health_is_eligible(&selector->health[index])
            || target->active_connections >= target->max_connections) {
            continue;
        }
        int allowed = circuit_breaker_try_acquire(&selector->breakers[index], now_ns);
        if (allowed <= 0) {
            continue;
        }
        target->active_connections++;
        pool->next_index = (index + 1U) % pool->count;
        if (selected_index != NULL) {
            *selected_index = index;
        }
        return target;
    }
    return NULL;
}

const backend_target_t *circuit_aware_pool_acquire_next(circuit_aware_pool_t *selector,
    uint64_t now_ns, size_t *selected_index)
{
    return circuit_aware_pool_acquire_next_excluding(selector, now_ns, SIZE_MAX, selected_index);
}
