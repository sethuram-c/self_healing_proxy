#include "backend_pool.h"

int backend_pool_init(
    backend_pool_t *pool,
    backend_target_t *targets,
    size_t count
)
{
    if (pool == NULL
        || targets == NULL
        || count == 0) {

        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        if (targets[i].name == NULL
            || targets[i].ip == NULL
            || targets[i].port == 0
            || targets[i].max_connections == 0) {

            return -1;
        }

        targets[i].active_connections = 0;
    }

    pool->targets = targets;
    pool->count = count;
    pool->next_index = 0;

    return 0;
}

const backend_target_t *backend_pool_acquire_next_excluding(
    backend_pool_t *pool,
    size_t excluded_index,
    size_t *selected_index
)
{
    if (pool == NULL
        || pool->targets == NULL
        || pool->count == 0) {

        return NULL;
    }

    for (size_t offset = 0; offset < pool->count; ++offset) {
        size_t index =
            (pool->next_index + offset) % pool->count;

        if (index == excluded_index) {
            continue;
        }

        backend_target_t *target =
            &pool->targets[index];

        if (target->active_connections
            >= target->max_connections) {

            continue;
        }

        target->active_connections++;
        pool->next_index =
            (index + 1U) % pool->count;

        if (selected_index != NULL) {
            *selected_index = index;
        }

        return target;
    }

    return NULL;
}

const backend_target_t *backend_pool_acquire_next(
    backend_pool_t *pool,
    size_t *selected_index
)
{
    return backend_pool_acquire_next_excluding(
        pool,
        SIZE_MAX,
        selected_index
    );
}

int backend_pool_release(
    backend_pool_t *pool,
    size_t index
)
{
    if (pool == NULL
        || pool->targets == NULL
        || index >= pool->count) {

        return -1;
    }

    backend_target_t *target =
        &pool->targets[index];

    if (target->active_connections == 0) {
        return -1;
    }

    target->active_connections--;
    return 0;
}

const backend_target_t *backend_pool_get(
    const backend_pool_t *pool,
    size_t index
)
{
    if (pool == NULL
        || pool->targets == NULL
        || index >= pool->count) {

        return NULL;
    }

    return &pool->targets[index];
}
