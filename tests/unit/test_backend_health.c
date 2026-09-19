#include "backend_health.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static backend_health_t fresh(void)
{
    backend_health_t health;
    const backend_health_config_t config = {
        .failure_threshold = 3,
        .recovery_success_threshold = 2,
        .cooldown_ns = 100,
    };
    assert(backend_health_init(&health, &config) == 0);
    return health;
}

static void test_initial_contract(void)
{
    backend_health_t health = fresh();
    assert(health.state == BACKEND_HEALTH_UNKNOWN);
    assert(backend_health_is_eligible(&health) == 1);
    assert(strcmp(backend_health_state_name(health.state), "UNKNOWN") == 0);
}

static void test_transient_failure_does_not_trip(void)
{
    backend_health_t health = fresh();
    assert(backend_health_record_success(&health, 10) == 0);
    assert(health.state == BACKEND_HEALTH_HEALTHY);
    assert(backend_health_record_failure(&health, 20) == 0);
    assert(health.state == BACKEND_HEALTH_SUSPECT);
    assert(health.consecutive_failures == 1);
    assert(backend_health_record_success(&health, 30) == 0);
    assert(health.state == BACKEND_HEALTH_HEALTHY);
    assert(health.consecutive_failures == 0);
}

static void test_threshold_trips_and_cooldown_is_required(void)
{
    backend_health_t health = fresh();
    assert(backend_health_record_success(&health, 10) == 0);
    assert(backend_health_record_failure(&health, 20) == 0);
    assert(backend_health_record_failure(&health, 30) == 0);
    assert(backend_health_record_failure(&health, 40) == 0);
    assert(health.state == BACKEND_HEALTH_UNHEALTHY);
    assert(backend_health_is_eligible(&health) == 0);
    assert(backend_health_tick(&health, 139) == 0);
    assert(health.state == BACKEND_HEALTH_UNHEALTHY);
    assert(backend_health_tick(&health, 140) == 0);
    assert(health.state == BACKEND_HEALTH_RECOVERING);
    assert(backend_health_is_eligible(&health) == 0);
}

static void test_recovery_requires_multiple_successes(void)
{
    backend_health_t health = fresh();
    assert(backend_health_record_failure(&health, 10) == 0);
    assert(backend_health_record_failure(&health, 20) == 0);
    assert(backend_health_record_failure(&health, 30) == 0);
    assert(backend_health_tick(&health, 130) == 0);
    assert(health.state == BACKEND_HEALTH_RECOVERING);
    assert(backend_health_record_success(&health, 140) == 0);
    assert(health.state == BACKEND_HEALTH_RECOVERING);
    assert(health.consecutive_successes == 1);
    assert(backend_health_record_success(&health, 150) == 0);
    assert(health.state == BACKEND_HEALTH_HEALTHY);
    assert(backend_health_is_eligible(&health) == 1);
}

static void test_recovery_failure_retrips_and_restarts_cooldown(void)
{
    backend_health_t health = fresh();
    assert(backend_health_record_failure(&health, 10) == 0);
    assert(backend_health_record_failure(&health, 20) == 0);
    assert(backend_health_record_failure(&health, 30) == 0);
    assert(backend_health_tick(&health, 130) == 0);
    assert(backend_health_record_success(&health, 140) == 0);
    assert(backend_health_record_failure(&health, 150) == 0);
    assert(health.state == BACKEND_HEALTH_UNHEALTHY);
    assert(backend_health_tick(&health, 249) == 0);
    assert(health.state == BACKEND_HEALTH_UNHEALTHY);
    assert(backend_health_tick(&health, 250) == 0);
    assert(health.state == BACKEND_HEALTH_RECOVERING);
}

static void test_unhealthy_success_cannot_bypass_cooldown(void)
{
    backend_health_t health = fresh();
    assert(backend_health_record_failure(&health, 10) == 0);
    assert(backend_health_record_failure(&health, 20) == 0);
    assert(backend_health_record_failure(&health, 30) == 0);
    assert(backend_health_record_success(&health, 40) == 0);
    assert(health.state == BACKEND_HEALTH_UNHEALTHY);
}

static void test_time_must_be_monotonic(void)
{
    backend_health_t health = fresh();
    assert(backend_health_record_success(&health, 100) == 0);
    assert(backend_health_record_failure(&health, 99) == -1);
    assert(backend_health_tick(&health, 98) == -1);
    assert(health.total_failures == 0);
}

static void test_invalid_config_is_rejected(void)
{
    backend_health_t health;
    backend_health_config_t config = {
        .failure_threshold = 0,
        .recovery_success_threshold = 2,
        .cooldown_ns = 100,
    };
    assert(backend_health_init(&health, &config) == -1);
    config.failure_threshold = 3;
    config.recovery_success_threshold = 0;
    assert(backend_health_init(&health, &config) == -1);
    config.recovery_success_threshold = 2;
    config.cooldown_ns = 0;
    assert(backend_health_init(&health, &config) == -1);
}

int main(void)
{
    test_initial_contract();
    test_transient_failure_does_not_trip();
    test_threshold_trips_and_cooldown_is_required();
    test_recovery_requires_multiple_successes();
    test_recovery_failure_retrips_and_restarts_cooldown();
    test_unhealthy_success_cannot_bypass_cooldown();
    test_time_must_be_monotonic();
    test_invalid_config_is_rejected();
    puts("backend health state tests passed");
    return 0;
}
