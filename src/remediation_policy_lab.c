#include "remediation_policy.h"

#include <stdio.h>
#include <string.h>

static void force_unhealthy(backend_health_t *health) {
    health->state = BACKEND_HEALTH_UNHEALTHY;
}

static void force_open(circuit_breaker_t *breaker) {
    breaker->state = CIRCUIT_BREAKER_OPEN;
}

static remediation_policy_config_t default_config(void) {
    remediation_policy_config_t c;
    c.initial_backoff_ns = 10U;
    c.max_backoff_ns = 40U;
    c.attempt_window_ns = 100U;
    c.max_attempts_per_window = 3U;
    c.require_health_unhealthy = 1;
    c.require_circuit_open = 1;
    return c;
}

static void init_signals(backend_health_t *health, circuit_breaker_t *breaker) {
    backend_health_config_t hc = {3U, 2U, 100U};
    circuit_breaker_config_t cc = {3U, 100U, 50U};
    (void)backend_health_init(health, &hc);
    (void)circuit_breaker_init(breaker, &cc);
}

static int print_decision(remediation_policy_t *p, backend_health_t *h, circuit_breaker_t *b, uint64_t now, const char *step) {
    remediation_decision_t d;
    if (remediation_policy_evaluate(p, h, b, now, &d) != 0) return -1;
    printf("STEP=%s NOW=%llu ACTION=%s REASON=%s ATTEMPTS=%zu NEXT_ALLOWED=%llu\n",
           step,
           (unsigned long long)now,
           remediation_decision_action_name(d.action),
           remediation_decision_reason_name(d.reason),
           d.attempts_in_window,
           (unsigned long long)d.next_allowed_ns);
    return d.action == REMEDIATION_DECISION_RESTART_BACKEND ? 1 : 0;
}

static int scenario_evidence(void) {
    remediation_policy_t p;
    remediation_policy_config_t cfg = default_config();
    backend_health_t h;
    circuit_breaker_t b;
    init_signals(&h, &b);
    if (remediation_policy_init(&p, &cfg) != 0) return 1;
    force_unhealthy(&h);
    if (print_decision(&p, &h, &b, 10U, "health_only") != 0) return 2;
    b.state = CIRCUIT_BREAKER_OPEN;
    if (print_decision(&p, &h, &b, 11U, "both_signals") != 1) return 3;
    puts("PHASE44_REMEDIATION_POLICY_OK");
    return 0;
}

static int scenario_backoff(void) {
    remediation_policy_t p;
    remediation_policy_config_t cfg = default_config();
    backend_health_t h;
    circuit_breaker_t b;
    init_signals(&h, &b);
    force_unhealthy(&h); force_open(&b);
    if (remediation_policy_init(&p, &cfg) != 0) return 1;
    if (print_decision(&p, &h, &b, 10U, "allowed_1") != 1) return 2;
    if (remediation_policy_record_attempt(&p, 10U) != 0) return 3;
    if (print_decision(&p, &h, &b, 19U, "blocked_1") != 0) return 4;
    if (print_decision(&p, &h, &b, 20U, "allowed_2") != 1) return 5;
    if (remediation_policy_record_attempt(&p, 20U) != 0) return 6;
    if (print_decision(&p, &h, &b, 39U, "blocked_2") != 0) return 7;
    if (print_decision(&p, &h, &b, 40U, "allowed_3") != 1) return 8;
    puts("PHASE44_REMEDIATION_POLICY_OK");
    return 0;
}

static int scenario_budget(void) {
    remediation_policy_t p;
    remediation_policy_config_t cfg = default_config();
    backend_health_t h;
    circuit_breaker_t b;
    init_signals(&h, &b);
    force_unhealthy(&h); force_open(&b);
    if (remediation_policy_init(&p, &cfg) != 0) return 1;
    if (remediation_policy_record_attempt(&p, 10U) != 0) return 2;
    if (remediation_policy_record_attempt(&p, 20U) != 0) return 3;
    if (remediation_policy_record_attempt(&p, 40U) != 0) return 4;
    if (print_decision(&p, &h, &b, 80U, "budget_exhausted") != 0) return 5;
    if (print_decision(&p, &h, &b, 111U, "window_aged") != 1) return 6;
    puts("PHASE44_REMEDIATION_POLICY_OK");
    return 0;
}

static int scenario_recovery(void) {
    remediation_policy_t p;
    remediation_policy_config_t cfg = default_config();
    backend_health_t h;
    circuit_breaker_t b;
    init_signals(&h, &b);
    force_unhealthy(&h); force_open(&b);
    if (remediation_policy_init(&p, &cfg) != 0) return 1;
    if (remediation_policy_record_attempt(&p, 10U) != 0) return 2;
    if (remediation_policy_record_attempt(&p, 20U) != 0) return 3;
    if (remediation_policy_record_recovery(&p, 21U) != 0) return 4;
    h.state = BACKEND_HEALTH_HEALTHY;
    b.state = CIRCUIT_BREAKER_CLOSED;
    if (print_decision(&p, &h, &b, 22U, "healthy_no_action") != 0) return 5;
    force_unhealthy(&h); force_open(&b);
    if (print_decision(&p, &h, &b, 23U, "relapse_allowed") != 1) return 6;
    puts("PHASE44_REMEDIATION_POLICY_OK");
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s --scenario evidence|backoff|budget|recovery\n", prog);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(argv[0]); return 0; }
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) { usage(argv[0]); return 2; }
    if (strcmp(argv[2], "evidence") == 0) return scenario_evidence();
    if (strcmp(argv[2], "backoff") == 0) return scenario_backoff();
    if (strcmp(argv[2], "budget") == 0) return scenario_budget();
    if (strcmp(argv[2], "recovery") == 0) return scenario_recovery();
    usage(argv[0]);
    return 2;
}
