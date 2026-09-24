#include "remediation_policy.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

static int time_valid(const remediation_policy_t *policy, uint64_t now_ns) {
    return policy != NULL && now_ns >= policy->last_observation_ns;
}

static void prune_attempts(remediation_policy_t *policy, uint64_t now_ns) {
    size_t first = 0U;
    while (first < policy->attempt_count) {
        const uint64_t t = policy->attempt_times[first];
        if (now_ns - t <= policy->config.attempt_window_ns) {
            break;
        }
        first++;
    }
    if (first > 0U) {
        const size_t remaining = policy->attempt_count - first;
        memmove(policy->attempt_times, policy->attempt_times + first, remaining * sizeof(policy->attempt_times[0]));
        policy->attempt_count = remaining;
    }
}

static uint64_t current_backoff_ns(const remediation_policy_t *policy) {
    uint64_t value = policy->config.initial_backoff_ns;
    size_t shifts = policy->consecutive_attempts;
    while (shifts > 0U && value < policy->config.max_backoff_ns) {
        if (value > policy->config.max_backoff_ns / 2U) {
            value = policy->config.max_backoff_ns;
            break;
        }
        value *= 2U;
        shifts--;
    }
    if (value > policy->config.max_backoff_ns) {
        value = policy->config.max_backoff_ns;
    }
    return value;
}

static int evidence_is_strong_enough(
    const remediation_policy_t *policy,
    const backend_health_t *health,
    const circuit_breaker_t *breaker
) {
    if (policy->config.require_health_unhealthy && health->state != BACKEND_HEALTH_UNHEALTHY) {
        return 0;
    }
    if (policy->config.require_circuit_open && breaker->state != CIRCUIT_BREAKER_OPEN) {
        return 0;
    }
    return 1;
}

int remediation_policy_init(remediation_policy_t *policy, const remediation_policy_config_t *config) {
    if (policy == NULL || config == NULL ||
        config->initial_backoff_ns == 0U ||
        config->max_backoff_ns < config->initial_backoff_ns ||
        config->attempt_window_ns == 0U ||
        config->max_attempts_per_window == 0U ||
        config->max_attempts_per_window > REMEDIATION_POLICY_MAX_ATTEMPTS ||
        (!config->require_health_unhealthy && !config->require_circuit_open)) {
        errno = EINVAL;
        return -1;
    }
    memset(policy, 0, sizeof(*policy));
    policy->config = *config;
    return 0;
}

int remediation_policy_evaluate(
    remediation_policy_t *policy,
    const backend_health_t *health,
    const circuit_breaker_t *breaker,
    uint64_t now_ns,
    remediation_decision_t *decision
) {
    if (policy == NULL || health == NULL || breaker == NULL || decision == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!time_valid(policy, now_ns)) {
        errno = ERANGE;
        return -1;
    }
    policy->last_observation_ns = now_ns;
    prune_attempts(policy, now_ns);

    memset(decision, 0, sizeof(*decision));
    decision->action = REMEDIATION_DECISION_NONE;
    decision->now_ns = now_ns;
    decision->next_allowed_ns = policy->next_allowed_ns;
    decision->attempts_in_window = policy->attempt_count;
    decision->backoff_ns = current_backoff_ns(policy);

    if (!evidence_is_strong_enough(policy, health, breaker)) {
        decision->reason = REMEDIATION_REASON_INSUFFICIENT_EVIDENCE;
        policy->suppressed_evidence++;
        return 0;
    }
    if (policy->attempt_count >= policy->config.max_attempts_per_window) {
        decision->reason = REMEDIATION_REASON_WINDOW_BUDGET_EXHAUSTED;
        policy->suppressed_budget++;
        return 0;
    }
    if (now_ns < policy->next_allowed_ns) {
        decision->reason = REMEDIATION_REASON_BACKOFF;
        policy->suppressed_backoff++;
        return 0;
    }

    decision->action = REMEDIATION_DECISION_RESTART_BACKEND;
    decision->reason = REMEDIATION_REASON_ALLOWED;
    return 0;
}

int remediation_policy_record_attempt(remediation_policy_t *policy, uint64_t now_ns) {
    uint64_t backoff;
    if (policy == NULL || !time_valid(policy, now_ns)) {
        errno = (policy == NULL) ? EINVAL : ERANGE;
        return -1;
    }
    policy->last_observation_ns = now_ns;
    prune_attempts(policy, now_ns);
    if (policy->attempt_count >= policy->config.max_attempts_per_window) {
        errno = EAGAIN;
        return -1;
    }
    policy->attempt_times[policy->attempt_count++] = now_ns;
    backoff = current_backoff_ns(policy);
    policy->consecutive_attempts++;
    policy->total_attempts++;
    if (UINT64_MAX - now_ns < backoff) {
        policy->next_allowed_ns = UINT64_MAX;
    } else {
        policy->next_allowed_ns = now_ns + backoff;
    }
    return 0;
}

int remediation_policy_record_recovery(remediation_policy_t *policy, uint64_t now_ns) {
    if (policy == NULL || !time_valid(policy, now_ns)) {
        errno = (policy == NULL) ? EINVAL : ERANGE;
        return -1;
    }
    policy->last_observation_ns = now_ns;
    prune_attempts(policy, now_ns);
    policy->consecutive_attempts = 0U;
    policy->next_allowed_ns = now_ns;
    policy->total_recoveries++;
    return 0;
}

const char *remediation_decision_action_name(remediation_decision_action_t action) {
    switch (action) {
        case REMEDIATION_DECISION_NONE: return "NONE";
        case REMEDIATION_DECISION_RESTART_BACKEND: return "RESTART_BACKEND";
    }
    return "INVALID";
}

const char *remediation_decision_reason_name(remediation_decision_reason_t reason) {
    switch (reason) {
        case REMEDIATION_REASON_INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
        case REMEDIATION_REASON_BACKOFF: return "BACKOFF";
        case REMEDIATION_REASON_WINDOW_BUDGET_EXHAUSTED: return "WINDOW_BUDGET_EXHAUSTED";
        case REMEDIATION_REASON_ALLOWED: return "ALLOWED";
    }
    return "INVALID";
}
