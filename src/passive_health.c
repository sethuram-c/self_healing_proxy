#include "passive_health.h"

#include <errno.h>
#include <stddef.h>

void passive_health_attempt_init(passive_health_attempt_t *attempt) {
    if (attempt == NULL) {
        return;
    }

    attempt->finalized = 0;
    attempt->outcome = PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE;
    attempt->evidence = PASSIVE_HEALTH_EVIDENCE_NEUTRAL;
}

int passive_health_classify(
    passive_health_outcome_t outcome,
    passive_health_evidence_t *evidence
) {
    if (evidence == NULL) {
        errno = EINVAL;
        return -1;
    }

    switch (outcome) {
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE:
            *evidence = PASSIVE_HEALTH_EVIDENCE_SUCCESS;
            return 0;

        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_REFUSED:
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_TIMEOUT:
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_ERROR:
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_TIMEOUT:
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESET_BEFORE_RESPONSE:
            *evidence = PASSIVE_HEALTH_EVIDENCE_FAILURE;
            return 0;

        case PASSIVE_HEALTH_OUTCOME_CLIENT_DISCONNECT:
        case PASSIVE_HEALTH_OUTCOME_CLIENT_REQUEST_REJECTED:
        case PASSIVE_HEALTH_OUTCOME_LOCAL_OVERLOAD:
        case PASSIVE_HEALTH_OUTCOME_LOCAL_IO_ERROR:
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_HTTP_STATUS:
            *evidence = PASSIVE_HEALTH_EVIDENCE_NEUTRAL;
            return 0;
    }

    errno = EINVAL;
    return -1;
}

int passive_health_apply(
    passive_health_attempt_t *attempt,
    backend_health_t *health,
    passive_health_outcome_t outcome,
    uint64_t now_ns,
    passive_health_evidence_t *evidence_out
) {
    passive_health_evidence_t evidence;
    int rc = 0;

    if (attempt == NULL || health == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (attempt->finalized) {
        errno = EALREADY;
        return -1;
    }

    if (passive_health_classify(outcome, &evidence) != 0) {
        return -1;
    }

    if (evidence == PASSIVE_HEALTH_EVIDENCE_SUCCESS) {
        rc = backend_health_record_success(health, now_ns);
    } else if (evidence == PASSIVE_HEALTH_EVIDENCE_FAILURE) {
        rc = backend_health_record_failure(health, now_ns);
    }

    if (rc != 0) {
        return -1;
    }

    attempt->finalized = 1;
    attempt->outcome = outcome;
    attempt->evidence = evidence;

    if (evidence_out != NULL) {
        *evidence_out = evidence;
    }

    return 0;
}

const char *passive_health_outcome_name(passive_health_outcome_t outcome) {
    switch (outcome) {
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE:
            return "UPSTREAM_RESPONSE_COMPLETE";
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_REFUSED:
            return "UPSTREAM_CONNECT_REFUSED";
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_TIMEOUT:
            return "UPSTREAM_CONNECT_TIMEOUT";
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_ERROR:
            return "UPSTREAM_CONNECT_ERROR";
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_TIMEOUT:
            return "UPSTREAM_RESPONSE_TIMEOUT";
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESET_BEFORE_RESPONSE:
            return "UPSTREAM_RESET_BEFORE_RESPONSE";
        case PASSIVE_HEALTH_OUTCOME_CLIENT_DISCONNECT:
            return "CLIENT_DISCONNECT";
        case PASSIVE_HEALTH_OUTCOME_CLIENT_REQUEST_REJECTED:
            return "CLIENT_REQUEST_REJECTED";
        case PASSIVE_HEALTH_OUTCOME_LOCAL_OVERLOAD:
            return "LOCAL_OVERLOAD";
        case PASSIVE_HEALTH_OUTCOME_LOCAL_IO_ERROR:
            return "LOCAL_IO_ERROR";
        case PASSIVE_HEALTH_OUTCOME_UPSTREAM_HTTP_STATUS:
            return "UPSTREAM_HTTP_STATUS";
    }

    return "INVALID";
}

const char *passive_health_evidence_name(passive_health_evidence_t evidence) {
    switch (evidence) {
        case PASSIVE_HEALTH_EVIDENCE_NEUTRAL:
            return "NEUTRAL";
        case PASSIVE_HEALTH_EVIDENCE_SUCCESS:
            return "SUCCESS";
        case PASSIVE_HEALTH_EVIDENCE_FAILURE:
            return "FAILURE";
    }

    return "INVALID";
}
