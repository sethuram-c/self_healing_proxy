#include "backend_health.h"
#include "passive_health.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

static backend_health_t new_health(size_t failure_threshold) {
    backend_health_t health;
    backend_health_config_t config = {failure_threshold, 2U, 100U};
    assert(backend_health_init(&health, &config) == 0);
    return health;
}

static void test_classification_contract(void) {
    passive_health_evidence_t evidence;

    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_SUCCESS);

    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_REFUSED, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_FAILURE);
    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_TIMEOUT, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_FAILURE);
    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_ERROR, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_FAILURE);
    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_TIMEOUT, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_FAILURE);
    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESET_BEFORE_RESPONSE, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_FAILURE);

    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_CLIENT_DISCONNECT, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_NEUTRAL);
    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_CLIENT_REQUEST_REJECTED, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_NEUTRAL);
    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_LOCAL_OVERLOAD, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_NEUTRAL);
    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_LOCAL_IO_ERROR, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_NEUTRAL);
    assert(passive_health_classify(PASSIVE_HEALTH_OUTCOME_UPSTREAM_HTTP_STATUS, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_NEUTRAL);
}

static void test_success_and_failure_drive_shared_state_machine(void) {
    backend_health_t health = new_health(2U);
    passive_health_attempt_t attempt;
    passive_health_evidence_t evidence;

    passive_health_attempt_init(&attempt);
    assert(passive_health_apply(&attempt, &health,
        PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, 10U, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_SUCCESS);
    assert(health.state == BACKEND_HEALTH_HEALTHY);
    assert(health.total_successes == 1U);

    passive_health_attempt_init(&attempt);
    assert(passive_health_apply(&attempt, &health,
        PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_TIMEOUT, 20U, &evidence) == 0);
    assert(evidence == PASSIVE_HEALTH_EVIDENCE_FAILURE);
    assert(health.state == BACKEND_HEALTH_SUSPECT);

    passive_health_attempt_init(&attempt);
    assert(passive_health_apply(&attempt, &health,
        PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESET_BEFORE_RESPONSE, 30U, &evidence) == 0);
    assert(health.state == BACKEND_HEALTH_UNHEALTHY);
    assert(health.total_failures == 2U);
}

static void test_neutral_outcomes_do_not_poison_backend(void) {
    backend_health_t health = new_health(2U);
    passive_health_attempt_t attempt;

    passive_health_attempt_init(&attempt);
    assert(passive_health_apply(&attempt, &health,
        PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, 10U, NULL) == 0);

    const passive_health_outcome_t neutral[] = {
        PASSIVE_HEALTH_OUTCOME_CLIENT_DISCONNECT,
        PASSIVE_HEALTH_OUTCOME_CLIENT_REQUEST_REJECTED,
        PASSIVE_HEALTH_OUTCOME_LOCAL_OVERLOAD,
        PASSIVE_HEALTH_OUTCOME_LOCAL_IO_ERROR,
        PASSIVE_HEALTH_OUTCOME_UPSTREAM_HTTP_STATUS
    };

    for (size_t i = 0; i < sizeof(neutral) / sizeof(neutral[0]); ++i) {
        passive_health_attempt_init(&attempt);
        assert(passive_health_apply(&attempt, &health, neutral[i], 20U + i, NULL) == 0);
    }

    assert(health.state == BACKEND_HEALTH_HEALTHY);
    assert(health.total_successes == 1U);
    assert(health.total_failures == 0U);
}

static void test_attempt_can_contribute_at_most_once(void) {
    backend_health_t health = new_health(3U);
    passive_health_attempt_t attempt;

    passive_health_attempt_init(&attempt);
    assert(passive_health_apply(&attempt, &health,
        PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_REFUSED, 10U, NULL) == 0);
    assert(health.total_failures == 1U);

    errno = 0;
    assert(passive_health_apply(&attempt, &health,
        PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_TIMEOUT, 20U, NULL) == -1);
    assert(errno == EALREADY);
    assert(health.total_failures == 1U);
}

static void test_invalid_inputs_are_rejected_without_mutation(void) {
    backend_health_t health = new_health(3U);
    passive_health_attempt_t attempt;
    passive_health_evidence_t evidence;

    passive_health_attempt_init(&attempt);
    errno = 0;
    assert(passive_health_classify((passive_health_outcome_t)999, &evidence) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(passive_health_apply(&attempt, &health,
        (passive_health_outcome_t)999, 10U, NULL) == -1);
    assert(errno == EINVAL);
    assert(attempt.finalized == 0);
    assert(health.total_failures == 0U);
    assert(health.total_successes == 0U);
}

int main(void) {
    test_classification_contract();
    test_success_and_failure_drive_shared_state_machine();
    test_neutral_outcomes_do_not_poison_backend();
    test_attempt_can_contribute_at_most_once();
    test_invalid_inputs_are_rejected_without_mutation();
    puts("passive health evidence tests passed");
    return 0;
}
