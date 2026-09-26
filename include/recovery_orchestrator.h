#ifndef RECOVERY_ORCHESTRATOR_H
#define RECOVERY_ORCHESTRATOR_H

#include <stddef.h>
#include <stdint.h>

#include "backend_health.h"
#include "circuit_breaker.h"
#include "health_probe.h"
#include "remediation_executor.h"

typedef enum {
    RECOVERY_ORCHESTRATOR_IDLE = 0,
    RECOVERY_ORCHESTRATOR_QUARANTINED,
    RECOVERY_ORCHESTRATOR_READY,
    RECOVERY_ORCHESTRATOR_FAILED
} recovery_orchestrator_state_t;

typedef enum {
    RECOVERY_FAILURE_NONE = 0,
    RECOVERY_FAILURE_RESTART_NOT_COMPLETED,
    RECOVERY_FAILURE_STARTUP_TIMEOUT,
    RECOVERY_FAILURE_STATE_ERROR
} recovery_failure_reason_t;

typedef struct {
    uint64_t startup_timeout_ns;
    size_t readiness_success_threshold;
} recovery_orchestrator_config_t;

typedef struct {
    recovery_orchestrator_state_t state;
    recovery_failure_reason_t failure_reason;
    recovery_orchestrator_config_t config;
    process_identity_t replacement_identity;
    uint64_t started_ns;
    uint64_t deadline_ns;
    uint64_t last_observation_ns;
    size_t consecutive_readiness_successes;
    uint64_t readiness_attempts;
    uint64_t readiness_successes;
    uint64_t readiness_failures;
} recovery_orchestrator_t;

int recovery_orchestrator_init(
    recovery_orchestrator_t *orchestrator,
    const recovery_orchestrator_config_t *config
);

int recovery_orchestrator_begin(
    recovery_orchestrator_t *orchestrator,
    const remediation_executor_result_t *restart_result,
    backend_health_t *health,
    circuit_breaker_t *breaker,
    uint64_t now_ns
);

int recovery_orchestrator_record_probe(
    recovery_orchestrator_t *orchestrator,
    health_probe_result_t result,
    backend_health_t *health,
    circuit_breaker_t *breaker,
    uint64_t now_ns
);

int recovery_orchestrator_tick(
    recovery_orchestrator_t *orchestrator,
    backend_health_t *health,
    circuit_breaker_t *breaker,
    uint64_t now_ns
);

int recovery_orchestrator_can_route(const recovery_orchestrator_t *orchestrator);
const char *recovery_orchestrator_state_name(recovery_orchestrator_state_t state);
const char *recovery_failure_reason_name(recovery_failure_reason_t reason);

#endif
