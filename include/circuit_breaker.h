#ifndef CIRCUIT_BREAKER_H
#define CIRCUIT_BREAKER_H

#include <stddef.h>
#include <stdint.h>

#define CIRCUIT_BREAKER_MAX_FAILURES 32U

typedef enum {
    CIRCUIT_BREAKER_CLOSED = 0,
    CIRCUIT_BREAKER_OPEN,
    CIRCUIT_BREAKER_HALF_OPEN
} circuit_breaker_state_t;

typedef struct {
    size_t failure_threshold;
    uint64_t failure_window_ns;
    uint64_t open_duration_ns;
} circuit_breaker_config_t;

typedef struct {
    circuit_breaker_state_t state;
    circuit_breaker_config_t config;
    uint64_t failure_times[CIRCUIT_BREAKER_MAX_FAILURES];
    size_t failure_count;
    uint64_t opened_at_ns;
    uint64_t last_observation_ns;
    int half_open_trial_in_flight;
    uint64_t opens;
    uint64_t closes;
    uint64_t rejected_requests;
} circuit_breaker_t;

int circuit_breaker_init(circuit_breaker_t *breaker, const circuit_breaker_config_t *config);
int circuit_breaker_tick(circuit_breaker_t *breaker, uint64_t now_ns);
int circuit_breaker_try_acquire(circuit_breaker_t *breaker, uint64_t now_ns);
int circuit_breaker_record_success(circuit_breaker_t *breaker, uint64_t now_ns);
int circuit_breaker_record_failure(circuit_breaker_t *breaker, uint64_t now_ns);
const char *circuit_breaker_state_name(circuit_breaker_state_t state);
size_t circuit_breaker_recent_failures(circuit_breaker_t *breaker, uint64_t now_ns);

#endif
