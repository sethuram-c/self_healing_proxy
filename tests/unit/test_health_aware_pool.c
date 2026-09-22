#include "backend_health.h"
#include "backend_pool.h"
#include "health_aware_pool.h"

#include <assert.h>
#include <stdio.h>

static const backend_health_config_t CFG = {
    .failure_threshold = 2,
    .recovery_success_threshold = 2,
    .cooldown_ns = 100,
};

static void init_fixture(
    backend_target_t targets[3],
    backend_pool_t *pool,
    backend_health_t health[3],
    health_aware_pool_t *selector
)
{
    targets[0] = (backend_target_t){"a", "127.0.0.1", 1, 0, 2};
    targets[1] = (backend_target_t){"b", "127.0.0.1", 2, 0, 2};
    targets[2] = (backend_target_t){"c", "127.0.0.1", 3, 0, 2};
    assert(backend_pool_init(pool, targets, 3) == 0);
    for (size_t i = 0; i < 3; ++i) {
        assert(backend_health_init(&health[i], &CFG) == 0);
    }
    assert(health_aware_pool_init(selector, pool, health, 3) == 0);
}

static void make_healthy(backend_health_t *health, uint64_t now)
{
    assert(backend_health_record_success(health, now) == 0);
}

static void make_unhealthy(backend_health_t *health, uint64_t now)
{
    assert(backend_health_record_failure(health, now) == 0);
    assert(backend_health_record_failure(health, now + 1) == 0);
    assert(health->state == BACKEND_HEALTH_UNHEALTHY);
}

static void test_init_validation(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;

    init_fixture(targets, &pool, health, &selector);
    assert(health_aware_pool_init(NULL, &pool, health, 3) == -1);
    assert(health_aware_pool_init(&selector, &pool, health, 2) == -1);
}

static void test_skips_unhealthy_and_recovering(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;
    size_t index;

    init_fixture(targets, &pool, health, &selector);
    for (size_t i = 0; i < 3; ++i) {
        make_healthy(&health[i], 1);
    }
    make_unhealthy(&health[1], 2);

    assert(health_aware_pool_acquire_next(&selector, &index) == &targets[0]);
    assert(index == 0);
    assert(backend_pool_release(&pool, index) == 0);

    assert(health_aware_pool_acquire_next(&selector, &index) == &targets[2]);
    assert(index == 2);
    assert(backend_pool_release(&pool, index) == 0);

    assert(backend_health_tick(&health[1], 103) == 0);
    assert(health[1].state == BACKEND_HEALTH_RECOVERING);
    assert(health_aware_pool_acquire_next(&selector, &index) == &targets[0]);
    assert(index == 0);
}

static void test_unknown_suspect_and_healthy_are_eligible(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;

    init_fixture(targets, &pool, health, &selector);
    make_healthy(&health[1], 1);
    assert(backend_health_record_failure(&health[2], 1) == 0);
    assert(health[2].state == BACKEND_HEALTH_SUSPECT);
    assert(health_aware_pool_count_eligible(&selector) == 3);
}

static void test_round_robin_fairness_among_eligible(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;
    const size_t expected[] = {0, 2, 0, 2, 0, 2};

    init_fixture(targets, &pool, health, &selector);
    for (size_t i = 0; i < 3; ++i) {
        make_healthy(&health[i], 1);
    }
    make_unhealthy(&health[1], 2);

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        size_t index;
        assert(health_aware_pool_acquire_next(&selector, &index) != NULL);
        assert(index == expected[i]);
        assert(backend_pool_release(&pool, index) == 0);
    }
}

static void test_all_ineligible_returns_null_without_cursor_move(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;
    size_t selected = 99;

    init_fixture(targets, &pool, health, &selector);
    for (size_t i = 0; i < 3; ++i) {
        make_healthy(&health[i], 1);
        make_unhealthy(&health[i], 2);
    }
    pool.next_index = 2;

    assert(health_aware_pool_acquire_next(&selector, &selected) == NULL);
    assert(pool.next_index == 2);
    assert(selected == 99);
    for (size_t i = 0; i < 3; ++i) {
        assert(targets[i].active_connections == 0);
    }
}

static void test_capacity_and_exclusion_both_apply(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;
    size_t index;

    init_fixture(targets, &pool, health, &selector);
    for (size_t i = 0; i < 3; ++i) {
        make_healthy(&health[i], 1);
    }
    targets[0].active_connections = targets[0].max_connections;
    make_unhealthy(&health[1], 2);

    assert(health_aware_pool_acquire_next(&selector, &index) == &targets[2]);
    assert(index == 2);
    assert(backend_pool_release(&pool, index) == 0);

    pool.next_index = 0;
    assert(health_aware_pool_acquire_next_excluding(&selector, 2, &index) == NULL);
}

static void test_recovered_backend_rejoins_rotation(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;
    size_t index;

    init_fixture(targets, &pool, health, &selector);
    for (size_t i = 0; i < 3; ++i) {
        make_healthy(&health[i], 1);
    }
    make_unhealthy(&health[1], 2);

    assert(health_aware_pool_acquire_next(&selector, &index) == &targets[0]);
    assert(backend_pool_release(&pool, index) == 0);
    assert(health_aware_pool_acquire_next(&selector, &index) == &targets[2]);
    assert(backend_pool_release(&pool, index) == 0);

    assert(backend_health_tick(&health[1], 103) == 0);
    assert(backend_health_record_success(&health[1], 104) == 0);
    assert(backend_health_record_success(&health[1], 105) == 0);
    assert(health[1].state == BACKEND_HEALTH_HEALTHY);

    assert(health_aware_pool_acquire_next(&selector, &index) == &targets[0]);
    assert(backend_pool_release(&pool, index) == 0);
    assert(health_aware_pool_acquire_next(&selector, &index) == &targets[1]);
    assert(backend_pool_release(&pool, index) == 0);
}

int main(void)
{
    test_init_validation();
    test_skips_unhealthy_and_recovering();
    test_unknown_suspect_and_healthy_are_eligible();
    test_round_robin_fairness_among_eligible();
    test_all_ineligible_returns_null_without_cursor_move();
    test_capacity_and_exclusion_both_apply();
    test_recovered_backend_rejoins_rotation();
    puts("health-aware backend selection tests passed");
    return 0;
}
