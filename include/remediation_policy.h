#ifndef REMEDIATION_POLICY_H
#define REMEDIATION_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "backend_health.h"
#include "circuit_breaker.h"

#define REMEDIATION_POLICY_MAX_ATTEMPTS 16U

typedef enum {
    REMEDIATION_DECISION_NONE = 0,
    REMEDIATION_DECISION_RESTART_BACKEND
} remediation_decision_action_t;

typedef enum {
    REMEDIATION_REASON_INSUFFICIENT_EVIDENCE = 0,
    REMEDIATION_REASON_BACKOFF,
    REMEDIATION_REASON_WINDOW_BUDGET_EXHAUSTED,
    REMEDIATION_REASON_ALLOWED
} remediation_decision_reason_t;

typedef struct {
    uint64_t initial_backoff_ns;
    uint64_t max_backoff_ns;
    uint64_t attempt_window_ns;
    size_t max_attempts_per_window;
    int require_health_unhealthy;
    int require_circuit_open;
} remediation_policy_config_t;

typedef struct {
    remediation_policy_config_t config;
    uint64_t attempt_times[REMEDIATION_POLICY_MAX_ATTEMPTS];
    size_t attempt_count;
    size_t consecutive_attempts;
    uint64_t next_allowed_ns;
    uint64_t last_observation_ns;
    uint64_t total_attempts;
    uint64_t total_recoveries;
    uint64_t suppressed_backoff;
    uint64_t suppressed_budget;
    uint64_t suppressed_evidence;
} remediation_policy_t;

typedef struct {
    remediation_decision_action_t action;
    remediation_decision_reason_t reason;
    uint64_t now_ns;
    uint64_t next_allowed_ns;
    size_t attempts_in_window;
    uint64_t backoff_ns;
} remediation_decision_t;

int remediation_policy_init(remediation_policy_t *policy, const remediation_policy_config_t *config);
int remediation_policy_evaluate(
    remediation_policy_t *policy,
    const backend_health_t *health,
    const circuit_breaker_t *breaker,
    uint64_t now_ns,
    remediation_decision_t *decision
);
int remediation_policy_record_attempt(remediation_policy_t *policy, uint64_t now_ns);
int remediation_policy_record_recovery(remediation_policy_t *policy, uint64_t now_ns);
const char *remediation_decision_action_name(remediation_decision_action_t action);
const char *remediation_decision_reason_name(remediation_decision_reason_t reason);

#endif
