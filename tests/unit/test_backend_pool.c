#include "backend_pool.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    backend_target_t targets[] = {
        {
            "backend-a",
            "127.0.0.1",
            9001,
            0,
            1
        },
        {
            "backend-b",
            "127.0.0.1",
            9002,
            0,
            1
        }
    };

    backend_pool_t pool;

    assert(backend_pool_init(
        &pool,
        targets,
        2
    ) == 0);

    size_t index = 99;

    const backend_target_t *target =
        backend_pool_acquire_next(
            &pool,
            &index
        );

    assert(target != NULL);
    assert(strcmp(target->name, "backend-a") == 0);
    assert(index == 0);
    assert(targets[0].active_connections == 1);

    target = backend_pool_acquire_next(
        &pool,
        &index
    );

    assert(target != NULL);
    assert(strcmp(target->name, "backend-b") == 0);
    assert(index == 1);
    assert(targets[1].active_connections == 1);

    assert(backend_pool_acquire_next(
        &pool,
        &index
    ) == NULL);

    assert(backend_pool_release(&pool, 0) == 0);
    assert(targets[0].active_connections == 0);

    target = backend_pool_acquire_next(
        &pool,
        &index
    );

    assert(target != NULL);
    assert(strcmp(target->name, "backend-a") == 0);
    assert(index == 0);

    assert(backend_pool_release(&pool, 1) == 0);
    assert(backend_pool_release(&pool, 0) == 0);

    assert(targets[0].active_connections == 0);
    assert(targets[1].active_connections == 0);

    assert(backend_pool_release(&pool, 0) == -1);
    assert(backend_pool_release(&pool, 2) == -1);

    assert(backend_pool_get(&pool, 0) == &targets[0]);
    assert(backend_pool_get(&pool, 1) == &targets[1]);
    assert(backend_pool_get(&pool, 2) == NULL);

    assert(backend_pool_init(NULL, targets, 2) == -1);
    assert(backend_pool_init(&pool, NULL, 2) == -1);
    assert(backend_pool_init(&pool, targets, 0) == -1);

    backend_target_t invalid[] = {
        {"bad", "127.0.0.1", 9003, 0, 0}
    };

    assert(backend_pool_init(
        &pool,
        invalid,
        1
    ) == -1);

    printf("backend pool admission tests passed\n");
    return 0;
}
