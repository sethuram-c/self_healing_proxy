#include "backend_health.h"

#include <limits.h>

static int timestamp_ok(const backend_health_t *health, uint64_t now_ns)
{
    return health != NULL && now_ns >= health->last_observation_ns;
}

static void transition(backend_health_t *health, backend_health_state_t next)
{
    if (health->state != next) {
        health->state = next;
        health->transitions++;
    }
}

int backend_health_init(
    backend_health_t *health,
    const backend_health_config_t *config
)
{
    if (health == NULL
        || config == NULL
        || config->failure_threshold == 0
        || config->recovery_success_threshold == 0
        || config->cooldown_ns == 0) {
        return -1;
    }

    health->state = BACKEND_HEALTH_UNKNOWN;
    health->config = *config;
    health->consecutive_failures = 0;
    health->consecutive_successes = 0;
    health->total_failures = 0;
    health->total_successes = 0;
    health->transitions = 0;
    health->last_observation_ns = 0;
    health->unhealthy_since_ns = 0;
    return 0;
}

int backend_health_record_success(backend_health_t *health, uint64_t now_ns)
{
    if (!timestamp_ok(health, now_ns)) {
        return -1;
    }

    health->last_observation_ns = now_ns;
    health->total_successes++;
    health->consecutive_failures = 0;

    switch (health->state) {
    case BACKEND_HEALTH_UNKNOWN:
    case BACKEND_HEALTH_SUSPECT:
        health->consecutive_successes = 0;
        transition(health, BACKEND_HEALTH_HEALTHY);
        break;

    case BACKEND_HEALTH_HEALTHY:
        health->consecutive_successes = 0;
        break;

    case BACKEND_HEALTH_UNHEALTHY:
        /* Cooldown owns the UNHEALTHY -> RECOVERING transition. */
        health->consecutive_successes = 0;
        break;

    case BACKEND_HEALTH_RECOVERING:
        if (health->consecutive_successes < SIZE_MAX) {
            health->consecutive_successes++;
        }
        if (health->consecutive_successes
            >= health->config.recovery_success_threshold) {
            health->consecutive_successes = 0;
            transition(health, BACKEND_HEALTH_HEALTHY);
        }
        break;

    default:
        return -1;
    }

    return 0;
}

int backend_health_record_failure(backend_health_t *health, uint64_t now_ns)
{
    if (!timestamp_ok(health, now_ns)) {
        return -1;
    }

    health->last_observation_ns = now_ns;
    health->total_failures++;
    health->consecutive_successes = 0;

    if (health->state == BACKEND_HEALTH_UNHEALTHY) {
        return 0;
    }

    if (health->state == BACKEND_HEALTH_RECOVERING) {
        health->consecutive_failures = 0;
        health->unhealthy_since_ns = now_ns;
        transition(health, BACKEND_HEALTH_UNHEALTHY);
        return 0;
    }

    if (health->consecutive_failures < SIZE_MAX) {
        health->consecutive_failures++;
    }

    if (health->consecutive_failures >= health->config.failure_threshold) {
        health->consecutive_failures = 0;
        health->unhealthy_since_ns = now_ns;
        transition(health, BACKEND_HEALTH_UNHEALTHY);
        return 0;
    }

    transition(health, BACKEND_HEALTH_SUSPECT);
    return 0;
}

int backend_health_tick(backend_health_t *health, uint64_t now_ns)
{
    if (!timestamp_ok(health, now_ns)) {
        return -1;
    }

    health->last_observation_ns = now_ns;

    if (health->state != BACKEND_HEALTH_UNHEALTHY) {
        return 0;
    }

    if (now_ns - health->unhealthy_since_ns < health->config.cooldown_ns) {
        return 0;
    }

    health->consecutive_failures = 0;
    health->consecutive_successes = 0;
    transition(health, BACKEND_HEALTH_RECOVERING);
    return 0;
}

const char *backend_health_state_name(backend_health_state_t state)
{
    switch (state) {
    case BACKEND_HEALTH_UNKNOWN:
        return "UNKNOWN";
    case BACKEND_HEALTH_HEALTHY:
        return "HEALTHY";
    case BACKEND_HEALTH_SUSPECT:
        return "SUSPECT";
    case BACKEND_HEALTH_UNHEALTHY:
        return "UNHEALTHY";
    case BACKEND_HEALTH_RECOVERING:
        return "RECOVERING";
    default:
        return "INVALID";
    }
}

int backend_health_is_eligible(const backend_health_t *health)
{
    if (health == NULL) {
        return 0;
    }

    return health->state == BACKEND_HEALTH_UNKNOWN
        || health->state == BACKEND_HEALTH_HEALTHY
        || health->state == BACKEND_HEALTH_SUSPECT;
}
