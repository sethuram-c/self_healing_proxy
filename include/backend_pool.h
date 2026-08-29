#ifndef BACKEND_POOL_H
#define BACKEND_POOL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const char *ip;
    uint16_t port;
    size_t active_connections;
    size_t max_connections;
} backend_target_t;

typedef struct {
    backend_target_t *targets;
    size_t count;
    size_t next_index;
} backend_pool_t;

int backend_pool_init(
    backend_pool_t *pool,
    backend_target_t *targets,
    size_t count
);

const backend_target_t *backend_pool_acquire_next(
    backend_pool_t *pool,
    size_t *selected_index
);

int backend_pool_release(
    backend_pool_t *pool,
    size_t index
);

const backend_target_t *backend_pool_get(
    const backend_pool_t *pool,
    size_t index
);

#endif
