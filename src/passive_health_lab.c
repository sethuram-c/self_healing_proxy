#include "backend_health.h"
#include "passive_health.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --scenario mixed|trip-recover|neutral\n",
        prog);
}

static int emit(
    backend_health_t *health,
    passive_health_outcome_t outcome,
    uint64_t now_ns,
    size_t step
) {
    passive_health_attempt_t attempt;
    passive_health_evidence_t evidence;

    passive_health_attempt_init(&attempt);
    if (passive_health_apply(&attempt, health, outcome, now_ns, &evidence) != 0) {
        perror("passive_health_apply");
        return -1;
    }

    printf(
        "STEP=%zu OUTCOME=%s EVIDENCE=%s STATE=%s ELIGIBLE=%d "
        "SUCCESSES=%" PRIu64 " FAILURES=%" PRIu64 "\n",
        step,
        passive_health_outcome_name(outcome),
        passive_health_evidence_name(evidence),
        backend_health_state_name(health->state),
        backend_health_is_eligible(health),
        health->total_successes,
        health->total_failures
    );
    return 0;
}

static int run_mixed(void) {
    const backend_health_config_t config = {3U, 2U, 100U};
    backend_health_t health;

    if (backend_health_init(&health, &config) != 0) {
        return -1;
    }

    if (emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, 10U, 1U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_CLIENT_DISCONNECT, 11U, 2U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_TIMEOUT, 20U, 3U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, 30U, 4U) != 0) {
        return -1;
    }

    return health.state == BACKEND_HEALTH_HEALTHY &&
        health.total_successes == 2U &&
        health.total_failures == 1U ? 0 : -1;
}

static int run_neutral(void) {
    const backend_health_config_t config = {2U, 2U, 100U};
    backend_health_t health;

    if (backend_health_init(&health, &config) != 0) {
        return -1;
    }

    if (emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, 10U, 1U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_CLIENT_REQUEST_REJECTED, 11U, 2U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_LOCAL_OVERLOAD, 12U, 3U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_HTTP_STATUS, 13U, 4U) != 0) {
        return -1;
    }

    return health.state == BACKEND_HEALTH_HEALTHY &&
        health.total_successes == 1U &&
        health.total_failures == 0U ? 0 : -1;
}

static int run_trip_recover(void) {
    const backend_health_config_t config = {3U, 2U, 100U};
    backend_health_t health;

    if (backend_health_init(&health, &config) != 0) {
        return -1;
    }

    if (emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, 10U, 1U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_REFUSED, 20U, 2U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_CONNECT_TIMEOUT, 30U, 3U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESET_BEFORE_RESPONSE, 40U, 4U) != 0) {
        return -1;
    }

    if (backend_health_tick(&health, 140U) != 0) {
        return -1;
    }
    printf("STEP=5 OUTCOME=COOLDOWN EVIDENCE=NEUTRAL STATE=%s ELIGIBLE=%d SUCCESSES=%" PRIu64 " FAILURES=%" PRIu64 "\n",
        backend_health_state_name(health.state),
        backend_health_is_eligible(&health),
        health.total_successes,
        health.total_failures);

    if (emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, 150U, 6U) != 0 ||
        emit(&health, PASSIVE_HEALTH_OUTCOME_UPSTREAM_RESPONSE_COMPLETE, 160U, 7U) != 0) {
        return -1;
    }

    return health.state == BACKEND_HEALTH_HEALTHY &&
        health.total_successes == 3U &&
        health.total_failures == 3U ? 0 : -1;
}

int main(int argc, char **argv) {
    const char *scenario = NULL;
    int rc;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (scenario == NULL) {
        usage(argv[0]);
        return 2;
    }

    if (strcmp(scenario, "mixed") == 0) {
        rc = run_mixed();
    } else if (strcmp(scenario, "trip-recover") == 0) {
        rc = run_trip_recover();
    } else if (strcmp(scenario, "neutral") == 0) {
        rc = run_neutral();
    } else {
        usage(argv[0]);
        return 2;
    }

    if (rc != 0) {
        fprintf(stderr, "scenario failed\n");
        return 1;
    }

    puts("PHASE41_PASSIVE_HEALTH_OK");
    return 0;
}
