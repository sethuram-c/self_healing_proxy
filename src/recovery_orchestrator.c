#include "recovery_orchestrator.h"

#include <errno.h>
#include <string.h>

static int time_ok(recovery_orchestrator_t *orchestrator, uint64_t now_ns)
{
    if (orchestrator == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (now_ns < orchestrator->last_observation_ns) {
        errno = ERANGE;
        return -1;
    }
    orchestrator->last_observation_ns = now_ns;
    return 0;
}

static void fail(
    recovery_orchestrator_t *orchestrator,
    recovery_failure_reason_t reason
)
{
    orchestrator->state = RECOVERY_ORCHESTRATOR_FAILED;
    orchestrator->failure_reason = reason;
    orchestrator->consecutive_readiness_successes = 0;
}

static int advance_dependencies(
    backend_health_t *health,
    circuit_breaker_t *breaker,
    uint64_t now_ns
)
{
    if (health == NULL || breaker == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (backend_health_tick(health, now_ns) != 0) {
        errno = ERANGE;
        return -1;
    }
    if (circuit_breaker_tick(breaker, now_ns) != 0) {
        return -1;
    }
    return 0;
}

static int maybe_ready(
    recovery_orchestrator_t *orchestrator,
    const backend_health_t *health,
    const circuit_breaker_t *breaker
)
{
    if (orchestrator->consecutive_readiness_successes
            >= orchestrator->config.readiness_success_threshold
        && health->state == BACKEND_HEALTH_HEALTHY
        && breaker->state == CIRCUIT_BREAKER_CLOSED) {
        orchestrator->state = RECOVERY_ORCHESTRATOR_READY;
        orchestrator->failure_reason = RECOVERY_FAILURE_NONE;
        return 1;
    }
    return 0;
}

int recovery_orchestrator_init(
    recovery_orchestrator_t *orchestrator,
    const recovery_orchestrator_config_t *config
)
{
    if (orchestrator == NULL || config == NULL
        || config->startup_timeout_ns == 0
        || config->readiness_success_threshold == 0) {
        errno = EINVAL;
        return -1;
    }

    memset(orchestrator, 0, sizeof(*orchestrator));
    orchestrator->state = RECOVERY_ORCHESTRATOR_IDLE;
    orchestrator->failure_reason = RECOVERY_FAILURE_NONE;
    orchestrator->config = *config;
    orchestrator->replacement_identity.pid = -1;
    return 0;
}

int recovery_orchestrator_begin(
    recovery_orchestrator_t *orchestrator,
    const remediation_executor_result_t *restart_result,
    backend_health_t *health,
    circuit_breaker_t *breaker,
    uint64_t now_ns
)
{
    if (orchestrator == NULL || restart_result == NULL
        || health == NULL || breaker == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (orchestrator->state != RECOVERY_ORCHESTRATOR_IDLE) {
        errno = EALREADY;
        return -1;
    }
    if (time_ok(orchestrator, now_ns) != 0) {
        return -1;
    }

    orchestrator->started_ns = now_ns;
    orchestrator->deadline_ns = UINT64_MAX - now_ns < orchestrator->config.startup_timeout_ns
        ? UINT64_MAX
        : now_ns + orchestrator->config.startup_timeout_ns;

    if (restart_result->status != REMEDIATION_EXECUTOR_RESTARTED
        || restart_result->new_identity.pid <= 0) {
        fail(orchestrator, RECOVERY_FAILURE_RESTART_NOT_COMPLETED);
        return 0;
    }

    orchestrator->replacement_identity = restart_result->new_identity;
    orchestrator->state = RECOVERY_ORCHESTRATOR_QUARANTINED;

    if (advance_dependencies(health, breaker, now_ns) != 0) {
        fail(orchestrator, RECOVERY_FAILURE_STATE_ERROR);
        return 0;
    }
    (void)maybe_ready(orchestrator, health, breaker);
    return 0;
}

int recovery_orchestrator_tick(
    recovery_orchestrator_t *orchestrator,
    backend_health_t *health,
    circuit_breaker_t *breaker,
    uint64_t now_ns
)
{
    if (orchestrator == NULL || health == NULL || breaker == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (time_ok(orchestrator, now_ns) != 0) {
        return -1;
    }
    if (orchestrator->state != RECOVERY_ORCHESTRATOR_QUARANTINED) {
        return 0;
    }
    if (now_ns >= orchestrator->deadline_ns) {
        fail(orchestrator, RECOVERY_FAILURE_STARTUP_TIMEOUT);
        return 0;
    }
    if (advance_dependencies(health, breaker, now_ns) != 0) {
        fail(orchestrator, RECOVERY_FAILURE_STATE_ERROR);
        return 0;
    }
    (void)maybe_ready(orchestrator, health, breaker);
    return 0;
}

int recovery_orchestrator_record_probe(
    recovery_orchestrator_t *orchestrator,
    health_probe_result_t result,
    backend_health_t *health,
    circuit_breaker_t *breaker,
    uint64_t now_ns
)
{
    int permit;

    if (orchestrator == NULL || health == NULL || breaker == NULL
        || result == HEALTH_PROBE_RESULT_NONE) {
        errno = EINVAL;
        return -1;
    }
    if (time_ok(orchestrator, now_ns) != 0) {
        return -1;
    }
    if (orchestrator->state != RECOVERY_ORCHESTRATOR_QUARANTINED) {
        errno = EINVAL;
        return -1;
    }
    if (now_ns >= orchestrator->deadline_ns) {
        fail(orchestrator, RECOVERY_FAILURE_STARTUP_TIMEOUT);
        return 0;
    }

    orchestrator->readiness_attempts++;
    if (advance_dependencies(health, breaker, now_ns) != 0) {
        fail(orchestrator, RECOVERY_FAILURE_STATE_ERROR);
        return 0;
    }

    if (result == HEALTH_PROBE_RESULT_SUCCESS) {
        orchestrator->readiness_successes++;
        orchestrator->consecutive_readiness_successes++;

        if (health->state == BACKEND_HEALTH_RECOVERING
            || health->state == BACKEND_HEALTH_UNKNOWN
            || health->state == BACKEND_HEALTH_SUSPECT
            || health->state == BACKEND_HEALTH_HEALTHY) {
            if (backend_health_record_success(health, now_ns) != 0) {
                fail(orchestrator, RECOVERY_FAILURE_STATE_ERROR);
                return 0;
            }
        }

        if (breaker->state == CIRCUIT_BREAKER_HALF_OPEN) {
            permit = circuit_breaker_try_acquire(breaker, now_ns);
            if (permit < 0) {
                fail(orchestrator, RECOVERY_FAILURE_STATE_ERROR);
                return 0;
            }
            if (permit == 1 && circuit_breaker_record_success(breaker, now_ns) != 0) {
                fail(orchestrator, RECOVERY_FAILURE_STATE_ERROR);
                return 0;
            }
        }
    } else {
        orchestrator->readiness_failures++;
        orchestrator->consecutive_readiness_successes = 0;
        /* During startup quarantine, a failed readiness probe means "not ready yet".
         * It does not count as fresh production health evidence. */
    }

    (void)maybe_ready(orchestrator, health, breaker);
    return 0;
}

int recovery_orchestrator_can_route(const recovery_orchestrator_t *orchestrator)
{
    return orchestrator != NULL && orchestrator->state == RECOVERY_ORCHESTRATOR_READY;
}

const char *recovery_orchestrator_state_name(recovery_orchestrator_state_t state)
{
    switch (state) {
    case RECOVERY_ORCHESTRATOR_IDLE: return "IDLE";
    case RECOVERY_ORCHESTRATOR_QUARANTINED: return "QUARANTINED";
    case RECOVERY_ORCHESTRATOR_READY: return "READY";
    case RECOVERY_ORCHESTRATOR_FAILED: return "FAILED";
    default: return "INVALID";
    }
}

const char *recovery_failure_reason_name(recovery_failure_reason_t reason)
{
    switch (reason) {
    case RECOVERY_FAILURE_NONE: return "NONE";
    case RECOVERY_FAILURE_RESTART_NOT_COMPLETED: return "RESTART_NOT_COMPLETED";
    case RECOVERY_FAILURE_STARTUP_TIMEOUT: return "STARTUP_TIMEOUT";
    case RECOVERY_FAILURE_STATE_ERROR: return "STATE_ERROR";
    default: return "INVALID";
    }
}
