#include "healing_coordinator.h"

#include <errno.h>
#include <string.h>

static int observe_time(healing_coordinator_t *coordinator, uint64_t now_ns) {
    if (coordinator->have_time && now_ns < coordinator->last_now_ns) {
        errno = ERANGE;
        return -1;
    }
    coordinator->last_now_ns = now_ns;
    coordinator->have_time = true;
    return 0;
}

static void prune_attempts(healing_coordinator_t *coordinator, uint64_t now_ns) {
    size_t keep_from = 0;
    while (keep_from < coordinator->attempt_count) {
        uint64_t t = coordinator->attempt_times[keep_from];
        if (now_ns >= t && now_ns - t > coordinator->config.attempt_window_ns) {
            keep_from++;
        } else {
            break;
        }
    }
    if (keep_from > 0) {
        size_t remaining = coordinator->attempt_count - keep_from;
        memmove(coordinator->attempt_times,
                coordinator->attempt_times + keep_from,
                remaining * sizeof(coordinator->attempt_times[0]));
        coordinator->attempt_count = remaining;
    }
}

int healing_coordinator_init(healing_coordinator_t *coordinator,
                             const healing_coordinator_config_t *config) {
    if (!coordinator || !config || config->backend_count == 0 ||
        config->backend_count > HEALING_COORDINATOR_MAX_BACKENDS ||
        config->max_concurrent == 0 || config->max_concurrent > config->backend_count ||
        config->max_attempts_per_window == 0 ||
        config->max_attempts_per_window > HEALING_COORDINATOR_MAX_ATTEMPTS ||
        config->attempt_window_ns == 0) {
        errno = EINVAL;
        return -1;
    }
    memset(coordinator, 0, sizeof(*coordinator));
    coordinator->config = *config;
    return 0;
}

int healing_coordinator_request(healing_coordinator_t *coordinator,
                                size_t backend_index,
                                uint64_t now_ns) {
    if (!coordinator || backend_index >= coordinator->config.backend_count) {
        errno = EINVAL;
        return -1;
    }
    if (observe_time(coordinator, now_ns) != 0) return -1;
    if (coordinator->pending[backend_index] || coordinator->in_progress[backend_index]) {
        errno = EALREADY;
        return -1;
    }
    coordinator->pending[backend_index] = true;
    coordinator->total_requested++;
    return 0;
}

int healing_coordinator_acquire_next(healing_coordinator_t *coordinator,
                                     uint64_t now_ns,
                                     size_t *backend_index,
                                     healing_admit_reason_t *reason) {
    if (!coordinator || !backend_index || !reason) {
        errno = EINVAL;
        return -1;
    }
    if (observe_time(coordinator, now_ns) != 0) return -1;
    prune_attempts(coordinator, now_ns);

    if (healing_coordinator_pending_count(coordinator) == 0) {
        *reason = HEALING_ADMIT_NO_PENDING;
        return 0;
    }
    if (coordinator->active_count >= coordinator->config.max_concurrent) {
        coordinator->total_denied_concurrency++;
        *reason = HEALING_ADMIT_CONCURRENCY_LIMIT;
        return 0;
    }
    if (coordinator->attempt_count >= coordinator->config.max_attempts_per_window) {
        coordinator->total_denied_budget++;
        *reason = HEALING_ADMIT_WINDOW_BUDGET;
        return 0;
    }

    for (size_t offset = 0; offset < coordinator->config.backend_count; offset++) {
        size_t idx = (coordinator->next_index + offset) % coordinator->config.backend_count;
        if (!coordinator->pending[idx]) continue;
        coordinator->pending[idx] = false;
        coordinator->in_progress[idx] = true;
        coordinator->active_count++;
        coordinator->attempt_times[coordinator->attempt_count++] = now_ns;
        coordinator->total_admitted++;
        coordinator->next_index = (idx + 1) % coordinator->config.backend_count;
        *backend_index = idx;
        *reason = HEALING_ADMIT_ALLOWED;
        return 1;
    }

    *reason = HEALING_ADMIT_NO_PENDING;
    return 0;
}

int healing_coordinator_complete(healing_coordinator_t *coordinator,
                                 size_t backend_index,
                                 uint64_t now_ns) {
    if (!coordinator || backend_index >= coordinator->config.backend_count) {
        errno = EINVAL;
        return -1;
    }
    if (observe_time(coordinator, now_ns) != 0) return -1;
    if (!coordinator->in_progress[backend_index]) {
        errno = EINVAL;
        return -1;
    }
    coordinator->in_progress[backend_index] = false;
    coordinator->active_count--;
    coordinator->total_completed++;
    return 0;
}

size_t healing_coordinator_pending_count(const healing_coordinator_t *coordinator) {
    if (!coordinator) return 0;
    size_t count = 0;
    for (size_t i = 0; i < coordinator->config.backend_count; i++) {
        if (coordinator->pending[i]) count++;
    }
    return count;
}

const char *healing_admit_reason_name(healing_admit_reason_t reason) {
    switch (reason) {
        case HEALING_ADMIT_NONE: return "NONE";
        case HEALING_ADMIT_ALLOWED: return "ALLOWED";
        case HEALING_ADMIT_CONCURRENCY_LIMIT: return "CONCURRENCY_LIMIT";
        case HEALING_ADMIT_WINDOW_BUDGET: return "WINDOW_BUDGET";
        case HEALING_ADMIT_NO_PENDING: return "NO_PENDING";
        default: return "UNKNOWN";
    }
}
