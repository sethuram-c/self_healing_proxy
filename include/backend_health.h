#ifndef BACKEND_HEALTH_H
#define BACKEND_HEALTH_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BACKEND_HEALTH_UNKNOWN = 0,
    BACKEND_HEALTH_HEALTHY,
    BACKEND_HEALTH_SUSPECT,
    BACKEND_HEALTH_UNHEALTHY,
    BACKEND_HEALTH_RECOVERING
} backend_health_state_t;

typedef struct {
    size_t failure_threshold;
    size_t recovery_success_threshold;
    uint64_t cooldown_ns;
} backend_health_config_t;

typedef struct {
    backend_health_state_t state;
    backend_health_config_t config;
    size_t consecutive_failures;
    size_t consecutive_successes;
    uint64_t total_failures;
    uint64_t total_successes;
    uint64_t transitions;
    uint64_t last_observation_ns;
    uint64_t unhealthy_since_ns;
} backend_health_t;

int backend_health_init(
    backend_health_t *health,
    const backend_health_config_t *config
);

int backend_health_record_success(
    backend_health_t *health,
    uint64_t now_ns
);

int backend_health_record_failure(
    backend_health_t *health,
    uint64_t now_ns
);

int backend_health_tick(
    backend_health_t *health,
    uint64_t now_ns
);

const char *backend_health_state_name(backend_health_state_t state);

int backend_health_is_eligible(const backend_health_t *health);

#endif
