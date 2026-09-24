#include "remediation_policy.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

static remediation_policy_config_t cfg(void) {
    remediation_policy_config_t c = {10U, 40U, 100U, 3U, 1, 1};
    return c;
}

static void signals(backend_health_t *h, circuit_breaker_t *b) {
    backend_health_config_t hc = {3U, 2U, 100U};
    circuit_breaker_config_t cc = {3U, 100U, 50U};
    assert(backend_health_init(h, &hc) == 0);
    assert(circuit_breaker_init(b, &cc) == 0);
}

int main(void) {
    remediation_policy_t p;
    remediation_policy_config_t c = cfg();
    backend_health_t h;
    circuit_breaker_t b;
    remediation_decision_t d;

    assert(remediation_policy_init(&p, &c) == 0);
    signals(&h, &b);
    h.state = BACKEND_HEALTH_UNHEALTHY;
    assert(remediation_policy_evaluate(&p, &h, &b, 1U, &d) == 0);
    assert(d.action == REMEDIATION_DECISION_NONE);
    assert(d.reason == REMEDIATION_REASON_INSUFFICIENT_EVIDENCE);

    b.state = CIRCUIT_BREAKER_OPEN;
    assert(remediation_policy_evaluate(&p, &h, &b, 2U, &d) == 0);
    assert(d.action == REMEDIATION_DECISION_RESTART_BACKEND);
    assert(remediation_policy_record_attempt(&p, 2U) == 0);
    assert(p.next_allowed_ns == 12U);

    assert(remediation_policy_evaluate(&p, &h, &b, 11U, &d) == 0);
    assert(d.reason == REMEDIATION_REASON_BACKOFF);
    assert(remediation_policy_evaluate(&p, &h, &b, 12U, &d) == 0);
    assert(d.action == REMEDIATION_DECISION_RESTART_BACKEND);
    assert(remediation_policy_record_attempt(&p, 12U) == 0);
    assert(p.next_allowed_ns == 32U);

    assert(remediation_policy_record_attempt(&p, 32U) == 0);
    assert(p.next_allowed_ns == 72U);
    assert(remediation_policy_evaluate(&p, &h, &b, 72U, &d) == 0);
    assert(d.reason == REMEDIATION_REASON_WINDOW_BUDGET_EXHAUSTED);

    assert(remediation_policy_evaluate(&p, &h, &b, 103U, &d) == 0);
    assert(d.action == REMEDIATION_DECISION_RESTART_BACKEND);
    assert(d.attempts_in_window == 2U);

    assert(remediation_policy_record_recovery(&p, 104U) == 0);
    assert(p.consecutive_attempts == 0U);
    assert(p.next_allowed_ns == 104U);

    errno = 0;
    assert(remediation_policy_evaluate(&p, &h, &b, 103U, &d) == -1);
    assert(errno == ERANGE);

    c.max_attempts_per_window = REMEDIATION_POLICY_MAX_ATTEMPTS + 1U;
    errno = 0;
    assert(remediation_policy_init(&p, &c) == -1);
    assert(errno == EINVAL);

    puts("remediation policy tests passed");
    return 0;
}
