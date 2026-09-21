#ifndef PASSIVE_HEALTH_H
#define PASSIVE_HEALTH_H

#include <stdint.h>

#include "backend_health.h"

typedef enum {
    PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE = 0,
    PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_REFUSED,
    PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_TIMEOUT,
    PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_ERROR,
    PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_TIMEOUT,
    PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESET_BEFORE_RESPONSE,
    PASSIVE_HEALTH_OUTCOME_CLIENT_DISCONNECT,
    PASSIVE_HEALTH_OUTCOME_CLIENT_REQUEST_REJECTED,
    PASSIVE_HEALTH_OUTCOME_LOCAL_OVERLOAD,
    PASSIVE_HEALTH_OUTCOME_LOCAL_IO_ERROR,
    PASSIVE_HEALTH_OUTCOME_UPSTREAM_HTTP_STATUS
} passive_health_outcome_t;

typedef enum {
    PASSIVE_HEALTH_EVIDENCE_NEUTRAL = 0,
    PASSIVE_HEALTH_EVIDENCE_SUCCESS,
    PASSIVE_HEALTH_EVIDENCE_FAILURE
} passive_health_evidence_t;

typedef struct {
    int finalized;
    passive_health_outcome_t outcome;
    passive_health_evidence_t evidence;
} passive_health_attempt_t;

void passive_health_attempt_init(passive_health_attempt_t *attempt);

int passive_health_classify(
    passive_health_outcome_t outcome,
    passive_health_evidence_t *evidence
);

int passive_health_apply(
    passive_health_attempt_t *attempt,
    backend_health_t *health,
    passive_health_outcome_t outcome,
    uint64_t now_ns,
    passive_health_evidence_t *evidence_out
);

const char *passive_health_outcome_name(passive_health_outcome_t outcome);
const char *passive_health_evidence_name(passive_health_evidence_t evidence);

#endif
