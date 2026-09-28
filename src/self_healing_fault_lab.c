#include "backend_health.h"
#include "circuit_breaker.h"
#include "healing_coordinator.h"
#include "healing_event.h"
#include "health_probe.h"
#include "recovery_orchestrator.h"
#include "remediation_policy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t sequence_no = 0;

static void emit_event(uint64_t now_ns, const char *backend, const char *name,
                       const backend_health_t *health, const circuit_breaker_t *breaker,
                       const recovery_orchestrator_t *recovery,
                       const char *action, const char *reason)
{
    char json[HEALING_EVENT_JSON_MAX];
    healing_event_t event;
    event.timestamp_ns = now_ns;
    event.sequence = ++sequence_no;
    event.backend = backend;
    event.event = name;
    event.health_state = health ? backend_health_state_name(health->state) : "NA";
    event.circuit_state = breaker ? circuit_breaker_state_name(breaker->state) : "NA";
    event.recovery_state = recovery ? recovery_orchestrator_state_name(recovery->state) : "NA";
    event.action = action ? action : "NONE";
    event.reason = reason ? reason : "NONE";
    if (healing_event_serialize(&event, json, sizeof(json)) != 0) {
        perror("healing_event_serialize");
        exit(1);
    }
    puts(json);
}

static int init_health_and_breaker(backend_health_t *health, circuit_breaker_t *breaker)
{
    backend_health_config_t hc = {3U, 2U, 10U};
    circuit_breaker_config_t cc = {3U, 100U, 10U};
    if (backend_health_init(health, &hc) != 0) return -1;
    if (circuit_breaker_init(breaker, &cc) != 0) return -1;
    return backend_health_record_success(health, 1U);
}

static int scenario_recover(void)
{
    backend_health_t health;
    circuit_breaker_t breaker;
    remediation_policy_t policy;
    remediation_policy_config_t pc = {1U, 8U, 100U, 3U, 1, 1};
    remediation_decision_t decision;
    healing_coordinator_t coordinator;
    healing_coordinator_config_t cc = {1U, 1U, 3U, 100U};
    healing_admit_reason_t admit_reason;
    size_t backend_index = 99U;
    remediation_executor_result_t restart_result;
    recovery_orchestrator_t recovery;
    recovery_orchestrator_config_t rc = {100U, 2U};
    const uint64_t failures[] = {10U, 20U, 30U};
    size_t i;

    if (init_health_and_breaker(&health, &breaker) != 0 ||
        remediation_policy_init(&policy, &pc) != 0 ||
        healing_coordinator_init(&coordinator, &cc) != 0 ||
        recovery_orchestrator_init(&recovery, &rc) != 0) return -1;

    emit_event(1U, "backend-a", "baseline_healthy", &health, &breaker, NULL, "NONE", "NONE");
    for (i = 0; i < 3U; i++) {
        if (backend_health_record_failure(&health, failures[i]) != 0 ||
            circuit_breaker_record_failure(&breaker, failures[i]) != 0) return -1;
        emit_event(failures[i], "backend-a", "fault_injected", &health, &breaker, NULL,
                   "NONE", "UPSTREAM_FAILURE");
    }
    if (health.state != BACKEND_HEALTH_UNHEALTHY || breaker.state != CIRCUIT_BREAKER_OPEN) return -1;

    if (remediation_policy_evaluate(&policy, &health, &breaker, 30U, &decision) != 0 ||
        decision.action != REMEDIATION_DECISION_RESTART_BACKEND) return -1;
    emit_event(30U, "backend-a", "remediation_approved", &health, &breaker, NULL,
               remediation_decision_action_name(decision.action),
               remediation_decision_reason_name(decision.reason));
    if (remediation_policy_record_attempt(&policy, 30U) != 0 ||
        healing_coordinator_request(&coordinator, 0U, 30U) != 0) return -1;
    if (healing_coordinator_acquire_next(&coordinator, 30U, &backend_index, &admit_reason) != 1 ||
        backend_index != 0U || admit_reason != HEALING_ADMIT_ALLOWED) return -1;
    emit_event(30U, "backend-a", "fleet_admitted", &health, &breaker, NULL,
               "RESTART_BACKEND", healing_admit_reason_name(admit_reason));

    memset(&restart_result, 0, sizeof(restart_result));
    restart_result.status = REMEDIATION_EXECUTOR_RESTARTED;
    restart_result.new_identity.pid = 4242;
    restart_result.new_identity.start_time_ticks = 99U;
    if (recovery_orchestrator_begin(&recovery, &restart_result, &health, &breaker, 40U) != 0) return -1;
    emit_event(40U, "backend-a", "replacement_quarantined", &health, &breaker, &recovery,
               "QUARANTINE", "RESTARTED");

    if (recovery_orchestrator_record_probe(&recovery, HEALTH_PROBE_RESULT_SUCCESS,
                                            &health, &breaker, 41U) != 0) return -1;
    emit_event(41U, "backend-a", "readiness_success", &health, &breaker, &recovery,
               "NONE", "PROBE_SUCCESS");
    if (recovery_orchestrator_record_probe(&recovery, HEALTH_PROBE_RESULT_SUCCESS,
                                            &health, &breaker, 42U) != 0) return -1;
    if (!recovery_orchestrator_can_route(&recovery)) return -1;
    if (healing_coordinator_complete(&coordinator, 0U, 42U) != 0 ||
        remediation_policy_record_recovery(&policy, 42U) != 0) return -1;
    emit_event(42U, "backend-a", "recovery_complete", &health, &breaker, &recovery,
               "ROUTE_ENABLED", "HEALTHY_AND_CLOSED");
    puts("PHASE48_SELF_HEALING_RECOVERY_OK");
    return 0;
}

static int scenario_storm(void)
{
    healing_coordinator_t coordinator;
    healing_coordinator_config_t cc = {3U, 1U, 2U, 100U};
    size_t backend = 99U;
    healing_admit_reason_t reason = HEALING_ADMIT_NONE;
    healing_event_t ev;
    char json[HEALING_EVENT_JSON_MAX];
    size_t i;

    if (healing_coordinator_init(&coordinator, &cc) != 0) return -1;
    for (i = 0; i < 3U; i++) {
        if (healing_coordinator_request(&coordinator, i, 10U) != 0) return -1;
    }
    if (healing_coordinator_acquire_next(&coordinator, 10U, &backend, &reason) != 1 || backend != 0U) return -1;
    ev = (healing_event_t){10U, ++sequence_no, "backend-a", "fleet_admitted", "UNHEALTHY", "OPEN", "NA", "RESTART_BACKEND", healing_admit_reason_name(reason)};
    if (healing_event_serialize(&ev, json, sizeof(json)) != 0) return -1;
    puts(json);
    if (healing_coordinator_acquire_next(&coordinator, 11U, &backend, &reason) != 0 || reason != HEALING_ADMIT_CONCURRENCY_LIMIT) return -1;
    ev = (healing_event_t){11U, ++sequence_no, "fleet", "restart_suppressed", "NA", "NA", "NA", "NONE", healing_admit_reason_name(reason)};
    if (healing_event_serialize(&ev, json, sizeof(json)) != 0) return -1;
    puts(json);
    if (healing_coordinator_complete(&coordinator, 0U, 20U) != 0) return -1;
    if (healing_coordinator_acquire_next(&coordinator, 20U, &backend, &reason) != 1 || backend != 1U) return -1;
    if (healing_coordinator_complete(&coordinator, 1U, 21U) != 0) return -1;
    if (healing_coordinator_acquire_next(&coordinator, 22U, &backend, &reason) != 0 || reason != HEALING_ADMIT_WINDOW_BUDGET) return -1;
    ev = (healing_event_t){22U, ++sequence_no, "fleet", "restart_suppressed", "NA", "NA", "NA", "NONE", healing_admit_reason_name(reason)};
    if (healing_event_serialize(&ev, json, sizeof(json)) != 0) return -1;
    puts(json);
    puts("PHASE48_RESTART_STORM_PROTECTION_OK");
    return 0;
}

static void usage(const char *argv0)
{
    printf("Usage: %s --scenario recover|storm\n", argv0);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "recover") == 0) return scenario_recover() == 0 ? 0 : 1;
    if (strcmp(argv[2], "storm") == 0) return scenario_storm() == 0 ? 0 : 1;
    usage(argv[0]);
    return 2;
}
