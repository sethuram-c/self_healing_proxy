#ifndef HEALING_COORDINATOR_H
#define HEALING_COORDINATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEALING_COORDINATOR_MAX_BACKENDS 32U
#define HEALING_COORDINATOR_MAX_ATTEMPTS 64U

typedef enum {
    HEALING_ADMIT_NONE = 0,
    HEALING_ADMIT_ALLOWED,
    HEALING_ADMIT_CONCURRENCY_LIMIT,
    HEALING_ADMIT_WINDOW_BUDGET,
    HEALING_ADMIT_NO_PENDING
} healing_admit_reason_t;

typedef struct {
    size_t backend_count;
    size_t max_concurrent;
    size_t max_attempts_per_window;
    uint64_t attempt_window_ns;
} healing_coordinator_config_t;

typedef struct {
    healing_coordinator_config_t config;
    bool pending[HEALING_COORDINATOR_MAX_BACKENDS];
    bool in_progress[HEALING_COORDINATOR_MAX_BACKENDS];
    uint64_t attempt_times[HEALING_COORDINATOR_MAX_ATTEMPTS];
    size_t attempt_count;
    size_t active_count;
    size_t next_index;
    uint64_t last_now_ns;
    bool have_time;
    uint64_t total_requested;
    uint64_t total_admitted;
    uint64_t total_completed;
    uint64_t total_denied_concurrency;
    uint64_t total_denied_budget;
} healing_coordinator_t;

int healing_coordinator_init(healing_coordinator_t *coordinator,
                             const healing_coordinator_config_t *config);
int healing_coordinator_request(healing_coordinator_t *coordinator,
                                size_t backend_index,
                                uint64_t now_ns);
int healing_coordinator_acquire_next(healing_coordinator_t *coordinator,
                                     uint64_t now_ns,
                                     size_t *backend_index,
                                     healing_admit_reason_t *reason);
int healing_coordinator_complete(healing_coordinator_t *coordinator,
                                 size_t backend_index,
                                 uint64_t now_ns);
size_t healing_coordinator_pending_count(const healing_coordinator_t *coordinator);
const char *healing_admit_reason_name(healing_admit_reason_t reason);

#endif
