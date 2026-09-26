#include "recovery_orchestrator.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void setup_unhealthy_open(backend_health_t *health, circuit_breaker_t *breaker)
{
    backend_health_config_t hc = {2, 2, 10};
    circuit_breaker_config_t cc = {2, 100, 20};

    assert(backend_health_init(health, &hc) == 0);
    assert(circuit_breaker_init(breaker, &cc) == 0);
    assert(backend_health_record_failure(health, 1) == 0);
    assert(backend_health_record_failure(health, 2) == 0);
    assert(health->state == BACKEND_HEALTH_UNHEALTHY);
    assert(circuit_breaker_record_failure(breaker, 1) == 0);
    assert(circuit_breaker_record_failure(breaker, 2) == 0);
    assert(breaker->state == CIRCUIT_BREAKER_OPEN);
}

static remediation_executor_result_t restarted_result(void)
{
    remediation_executor_result_t result;
    memset(&result, 0, sizeof(result));
    result.status = REMEDIATION_EXECUTOR_RESTARTED;
    result.new_identity.pid = 4242;
    result.new_identity.start_time_ticks = 99;
    return result;
}

static void test_two_readiness_successes_rejoin(void)
{
    recovery_orchestrator_t o;
    recovery_orchestrator_config_t cfg = {100, 2};
    remediation_executor_result_t rr = restarted_result();
    backend_health_t health;
    circuit_breaker_t breaker;

    setup_unhealthy_open(&health, &breaker);
    assert(recovery_orchestrator_init(&o, &cfg) == 0);
    assert(recovery_orchestrator_begin(&o, &rr, &health, &breaker, 30) == 0);
    assert(o.state == RECOVERY_ORCHESTRATOR_QUARANTINED);
    assert(health.state == BACKEND_HEALTH_RECOVERING);
    assert(breaker.state == CIRCUIT_BREAKER_HALF_OPEN);
    assert(recovery_orchestrator_can_route(&o) == 0);

    assert(recovery_orchestrator_record_probe(
        &o, HEALTH_PROBE_RESULT_SUCCESS, &health, &breaker, 31) == 0);
    assert(o.state == RECOVERY_ORCHESTRATOR_QUARANTINED);
    assert(health.state == BACKEND_HEALTH_RECOVERING);
    assert(breaker.state == CIRCUIT_BREAKER_CLOSED);

    assert(recovery_orchestrator_record_probe(
        &o, HEALTH_PROBE_RESULT_SUCCESS, &health, &breaker, 32) == 0);
    assert(o.state == RECOVERY_ORCHESTRATOR_READY);
    assert(health.state == BACKEND_HEALTH_HEALTHY);
    assert(breaker.state == CIRCUIT_BREAKER_CLOSED);
    assert(recovery_orchestrator_can_route(&o) == 1);
}

static void test_failed_probes_do_not_poison_startup_health(void)
{
    recovery_orchestrator_t o;
    recovery_orchestrator_config_t cfg = {100, 2};
    remediation_executor_result_t rr = restarted_result();
    backend_health_t health;
    circuit_breaker_t breaker;

    setup_unhealthy_open(&health, &breaker);
    assert(recovery_orchestrator_init(&o, &cfg) == 0);
    assert(recovery_orchestrator_begin(&o, &rr, &health, &breaker, 30) == 0);

    assert(recovery_orchestrator_record_probe(
        &o, HEALTH_PROBE_RESULT_REFUSED, &health, &breaker, 31) == 0);
    assert(health.state == BACKEND_HEALTH_RECOVERING);
    assert(health.total_failures == 2);

    assert(recovery_orchestrator_record_probe(
        &o, HEALTH_PROBE_RESULT_SUCCESS, &health, &breaker, 32) == 0);
    assert(recovery_orchestrator_record_probe(
        &o, HEALTH_PROBE_RESULT_TIMEOUT, &health, &breaker, 33) == 0);
    assert(o.consecutive_readiness_successes == 0);
    assert(health.total_failures == 2);

    assert(recovery_orchestrator_record_probe(
        &o, HEALTH_PROBE_RESULT_SUCCESS, &health, &breaker, 34) == 0);
    assert(health.state == BACKEND_HEALTH_HEALTHY);
    assert(o.state == RECOVERY_ORCHESTRATOR_QUARANTINED);
    assert(recovery_orchestrator_record_probe(
        &o, HEALTH_PROBE_RESULT_SUCCESS, &health, &breaker, 35) == 0);
    assert(o.state == RECOVERY_ORCHESTRATOR_READY);
}

static void test_startup_deadline_fails_closed(void)
{
    recovery_orchestrator_t o;
    recovery_orchestrator_config_t cfg = {50, 2};
    remediation_executor_result_t rr = restarted_result();
    backend_health_t health;
    circuit_breaker_t breaker;

    setup_unhealthy_open(&health, &breaker);
    assert(recovery_orchestrator_init(&o, &cfg) == 0);
    assert(recovery_orchestrator_begin(&o, &rr, &health, &breaker, 30) == 0);
    assert(recovery_orchestrator_tick(&o, &health, &breaker, 79) == 0);
    assert(o.state == RECOVERY_ORCHESTRATOR_QUARANTINED);
    assert(recovery_orchestrator_tick(&o, &health, &breaker, 80) == 0);
    assert(o.state == RECOVERY_ORCHESTRATOR_FAILED);
    assert(o.failure_reason == RECOVERY_FAILURE_STARTUP_TIMEOUT);
    assert(recovery_orchestrator_can_route(&o) == 0);
}

static void test_failed_restart_never_enters_quarantine(void)
{
    recovery_orchestrator_t o;
    recovery_orchestrator_config_t cfg = {50, 2};
    remediation_executor_result_t rr = restarted_result();
    backend_health_t health;
    circuit_breaker_t breaker;

    rr.status = REMEDIATION_EXECUTOR_SPAWN_FAILED;
    rr.new_identity.pid = -1;
    setup_unhealthy_open(&health, &breaker);
    assert(recovery_orchestrator_init(&o, &cfg) == 0);
    assert(recovery_orchestrator_begin(&o, &rr, &health, &breaker, 30) == 0);
    assert(o.state == RECOVERY_ORCHESTRATOR_FAILED);
    assert(o.failure_reason == RECOVERY_FAILURE_RESTART_NOT_COMPLETED);
    assert(recovery_orchestrator_can_route(&o) == 0);
}

static void test_backward_time_rejected(void)
{
    recovery_orchestrator_t o;
    recovery_orchestrator_config_t cfg = {50, 2};
    remediation_executor_result_t rr = restarted_result();
    backend_health_t health;
    circuit_breaker_t breaker;

    setup_unhealthy_open(&health, &breaker);
    assert(recovery_orchestrator_init(&o, &cfg) == 0);
    assert(recovery_orchestrator_begin(&o, &rr, &health, &breaker, 30) == 0);
    errno = 0;
    assert(recovery_orchestrator_tick(&o, &health, &breaker, 29) == -1);
    assert(errno == ERANGE);
}

int main(void)
{
    test_two_readiness_successes_rejoin();
    test_failed_probes_do_not_poison_startup_health();
    test_startup_deadline_fails_closed();
    test_failed_restart_never_enters_quarantine();
    test_backward_time_rejected();
    puts("recovery orchestrator tests passed");
    return 0;
}
