#include "backend_pool.h"

int backend_pool_init(
    backend_pool_t *pool,
    const backend_target_t *targets,
    size_t count
)
{
    if (pool == NULL
        || targets == NULL
        || count == 0) {

        return -1;
    }

    pool->targets = targets;
    pool->count = count;
    pool->next_index = 0;

    return 0;
}

const backend_target_t *backend_pool_next(
    backend_pool_t *pool,
    size_t *selected_index
)
{
    if (pool == NULL
        || pool->targets == NULL
        || pool->count == 0) {

        return NULL;
    }

    size_t index = pool->next_index;

    pool->next_index =
        (pool->next_index + 1U) % pool->count;

    if (selected_index != NULL) {
        *selected_index = index;
    }

    return &pool->targets[index];
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
