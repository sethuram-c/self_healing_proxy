#include "backend_health.h"
#include "backend_pool.h"
#include "circuit_aware_pool.h"
#include "circuit_breaker.h"

#include <stdio.h>
#include <string.h>

static void init_fixture(backend_pool_t *pool, backend_target_t targets[3], backend_health_t health[3], circuit_breaker_t breakers[3])
{
    const char *names[3] = {"backend-a", "backend-b", "backend-c"};
    backend_health_config_t hc = {3, 2, 100};
    circuit_breaker_config_t cc = {3, 100, 50};
    for (size_t i = 0; i < 3; ++i) {
        targets[i] = (backend_target_t){names[i], "127.0.0.1", (uint16_t)(9001+i), 0, 16};
        backend_health_init(&health[i], &hc);
        backend_health_record_success(&health[i], 1);
        circuit_breaker_init(&breakers[i], &cc);
    }
    backend_pool_init(pool, targets, 3);
}

static void release_selected(backend_pool_t *pool, size_t idx) { (void)backend_pool_release(pool, idx); }

int main(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) {
        fprintf(stderr, "Usage: %s --scenario open-skip|window|half-open\n", argv[0]);
        return argc == 2 && strcmp(argv[1], "--help") == 0 ? 0 : 2;
    }
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3]; circuit_breaker_t breakers[3];
    circuit_aware_pool_t selector; size_t idx = 0;
    init_fixture(&pool, targets, health, breakers);
    circuit_aware_pool_init(&selector, &pool, health, breakers, 3);

    if (strcmp(argv[2], "open-skip") == 0) {
        circuit_breaker_record_failure(&breakers[1], 10);
        circuit_breaker_record_failure(&breakers[1], 20);
        circuit_breaker_record_failure(&breakers[1], 30);
        for (int i = 0; i < 4; ++i) {
            const backend_target_t *t = circuit_aware_pool_acquire_next(&selector, 31, &idx);
            printf("PICK=%s\n", t ? t->name : "NONE");
            if (t) release_selected(&pool, idx);
        }
        printf("B_STATE=%s\n", circuit_breaker_state_name(breakers[1].state));
    } else if (strcmp(argv[2], "window") == 0) {
        circuit_breaker_record_failure(&breakers[1], 10);
        circuit_breaker_record_failure(&breakers[1], 120);
        circuit_breaker_record_failure(&breakers[1], 130);
        printf("RECENT_FAILURES=%zu\n", circuit_breaker_recent_failures(&breakers[1], 130));
        printf("B_STATE=%s\n", circuit_breaker_state_name(breakers[1].state));
    } else if (strcmp(argv[2], "half-open") == 0) {
        circuit_breaker_record_failure(&breakers[1], 10);
        circuit_breaker_record_failure(&breakers[1], 20);
        circuit_breaker_record_failure(&breakers[1], 30);
        circuit_breaker_tick(&breakers[1], 80);
        printf("B_STATE=%s\n", circuit_breaker_state_name(breakers[1].state));
        pool.next_index = 1;
        const backend_target_t *t = circuit_aware_pool_acquire_next(&selector, 80, &idx);
        printf("TRIAL=%s\n", t ? t->name : "NONE");
        const backend_target_t *t2 = circuit_aware_pool_acquire_next(&selector, 80, NULL);
        printf("SECOND=%s\n", t2 ? t2->name : "NONE");
        if (t) { release_selected(&pool, idx); circuit_breaker_record_success(&breakers[1], 81); }
        printf("B_FINAL=%s\n", circuit_breaker_state_name(breakers[1].state));
    } else {
        fprintf(stderr, "unknown scenario\n"); return 2;
    }
    puts("PHASE43_CIRCUIT_BREAKER_OK");
    return 0;
}
