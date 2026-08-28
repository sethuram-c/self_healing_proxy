#include "backend_pool.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const backend_target_t targets[] = {
        {"backend-a", "127.0.0.1", 9001},
        {"backend-b", "127.0.0.1", 9002}
    };

    backend_pool_t pool;

    assert(backend_pool_init(
        &pool,
        targets,
        2
    ) == 0);

    const char *expected[] = {
        "backend-a",
        "backend-b",
        "backend-a",
        "backend-b",
        "backend-a",
        "backend-b"
    };

    for (size_t i = 0; i < 6; ++i) {
        size_t index = 99;

        const backend_target_t *target =
            backend_pool_next(
                &pool,
                &index
            );

        assert(target != NULL);
        assert(strcmp(target->name, expected[i]) == 0);
        assert(index == (i % 2));
    }

    assert(backend_pool_get(&pool, 0) == &targets[0]);
    assert(backend_pool_get(&pool, 1) == &targets[1]);
    assert(backend_pool_get(&pool, 2) == NULL);

    assert(backend_pool_init(NULL, targets, 2) == -1);
    assert(backend_pool_init(&pool, NULL, 2) == -1);
    assert(backend_pool_init(&pool, targets, 0) == -1);

    printf("backend pool tests passed\n");
    return 0;
}
