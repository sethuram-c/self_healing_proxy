#include "edge_aware_pool.h"

#include <stddef.h>

static int circuit_can_attempt(circuit_breaker_t *breaker, uint64_t now_ns)
{
    if (circuit_breaker_tick(breaker, now_ns) != 0) {
        return 0;
    }
    if (breaker->state == CIRCUIT_BREAKER_CLOSED) {
        return 1;
    }
    return breaker->state == CIRCUIT_BREAKER_HALF_OPEN && !breaker->half_open_trial_in_flight;
}

int edge_aware_pool_init(edge_aware_pool_t *selector, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers,
    edge_locality_t *localities, size_t count)
{
    if (selector == NULL || pool == NULL || pool->targets == NULL || pool->count == 0
        || health == NULL || breakers == NULL || localities == NULL || count != pool->count) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (edge_locality_validate(&localities[i]) != 0) {
            return -1;
        }
    }
    selector->pool = pool;
    selector->health = health;
    selector->breakers = breakers;
    selector->localities = localities;
    selector->count = count;
    return 0;
}

const backend_target_t *edge_aware_pool_acquire_next(edge_aware_pool_t *selector,
    const edge_locality_t *request_locality, uint64_t now_ns,
    size_t *selected_index, edge_locality_rank_t *selected_rank)
{
    if (selector == NULL || selector->pool == NULL || request_locality == NULL
        || edge_locality_validate(request_locality) != 0 || selector->count != selector->pool->count) {
        return NULL;
    }

    backend_pool_t *pool = selector->pool;
    edge_locality_rank_t best_rank = EDGE_LOCALITY_REMOTE;
    int found = 0;

    for (size_t i = 0; i < pool->count; ++i) {
        backend_target_t *target = &pool->targets[i];
        if (!backend_health_is_eligible(&selector->health[i])
            || target->active_connections >= target->max_connections
            || !circuit_can_attempt(&selector->breakers[i], now_ns)) {
            continue;
        }
        edge_locality_rank_t rank = edge_locality_rank(request_locality, &selector->localities[i]);
        if (!found || rank < best_rank) {
            best_rank = rank;
            found = 1;
        }
    }
    if (!found) {
        return NULL;
    }

    for (size_t offset = 0; offset < pool->count; ++offset) {
        size_t index = (pool->next_index + offset) % pool->count;
        backend_target_t *target = &pool->targets[index];
        if (!backend_health_is_eligible(&selector->health[index])
            || target->active_connections >= target->max_connections
            || edge_locality_rank(request_locality, &selector->localities[index]) != best_rank
            || !circuit_can_attempt(&selector->breakers[index], now_ns)) {
            continue;
        }
        if (circuit_breaker_try_acquire(&selector->breakers[index], now_ns) <= 0) {
            continue;
        }
        target->active_connections++;
        pool->next_index = (index + 1U) % pool->count;
        if (selected_index != NULL) *selected_index = index;
        if (selected_rank != NULL) *selected_rank = best_rank;
        return target;
    }
    return NULL;
}
