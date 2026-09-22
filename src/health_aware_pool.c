#include "health_aware_pool.h"

#include <stdint.h>

int health_aware_pool_init(
    health_aware_pool_t *selector,
    backend_pool_t *pool,
    backend_health_t *health,
    size_t health_count
)
{
    if (selector == NULL
        || pool == NULL
        || pool->targets == NULL
        || pool->count == 0
        || health == NULL
        || health_count != pool->count) {
        return -1;
    }

    selector->pool = pool;
    selector->health = health;
    selector->health_count = health_count;
    return 0;
}

static int target_is_selectable(
    const health_aware_pool_t *selector,
    size_t index,
    size_t excluded_index
)
{
    const backend_target_t *target;

    if (index == excluded_index) {
        return 0;
    }

    if (!backend_health_is_eligible(&selector->health[index])) {
        return 0;
    }

    target = &selector->pool->targets[index];
    return target->active_connections < target->max_connections;
}

const backend_target_t *health_aware_pool_acquire_next_excluding(
    health_aware_pool_t *selector,
    size_t excluded_index,
    size_t *selected_index
)
{
    backend_pool_t *pool;

    if (selector == NULL
        || selector->pool == NULL
        || selector->health == NULL
        || selector->health_count != selector->pool->count
        || selector->pool->targets == NULL
        || selector->pool->count == 0) {
        return NULL;
    }

    pool = selector->pool;

    for (size_t offset = 0; offset < pool->count; ++offset) {
        size_t index = (pool->next_index + offset) % pool->count;
        backend_target_t *target;

        if (!target_is_selectable(selector, index, excluded_index)) {
            continue;
        }

        target = &pool->targets[index];
        target->active_connections++;
        pool->next_index = (index + 1U) % pool->count;

        if (selected_index != NULL) {
            *selected_index = index;
        }

        return target;
    }

    return NULL;
}

const backend_target_t *health_aware_pool_acquire_next(
    health_aware_pool_t *selector,
    size_t *selected_index
)
{
    return health_aware_pool_acquire_next_excluding(
        selector,
        SIZE_MAX,
        selected_index
    );
}

size_t health_aware_pool_count_eligible(
    const health_aware_pool_t *selector
)
{
    size_t eligible = 0;

    if (selector == NULL
        || selector->pool == NULL
        || selector->health == NULL
        || selector->health_count != selector->pool->count) {
        return 0;
    }

    for (size_t i = 0; i < selector->pool->count; ++i) {
        if (backend_health_is_eligible(&selector->health[i])) {
            eligible++;
        }
    }

    return eligible;
}
