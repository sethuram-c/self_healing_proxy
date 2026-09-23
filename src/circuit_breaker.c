#include "circuit_breaker.h"

#include <errno.h>
#include <string.h>

static int check_time(circuit_breaker_t *breaker, uint64_t now_ns)
{
    if (breaker == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (now_ns < breaker->last_observation_ns) {
        errno = EINVAL;
        return -1;
    }
    breaker->last_observation_ns = now_ns;
    return 0;
}

static void purge_old(circuit_breaker_t *breaker, uint64_t now_ns)
{
    size_t keep = 0;
    for (size_t i = 0; i < breaker->failure_count; ++i) {
        uint64_t ts = breaker->failure_times[i];
        if (now_ns >= ts && now_ns - ts <= breaker->config.failure_window_ns) {
            breaker->failure_times[keep++] = ts;
        }
    }
    breaker->failure_count = keep;
}

static void open_breaker(circuit_breaker_t *breaker, uint64_t now_ns)
{
    breaker->state = CIRCUIT_BREAKER_OPEN;
    breaker->opened_at_ns = now_ns;
    breaker->half_open_trial_in_flight = 0;
    breaker->failure_count = 0;
    breaker->opens++;
}

int circuit_breaker_init(circuit_breaker_t *breaker, const circuit_breaker_config_t *config)
{
    if (breaker == NULL || config == NULL
        || config->failure_threshold == 0
        || config->failure_threshold > CIRCUIT_BREAKER_MAX_FAILURES
        || config->failure_window_ns == 0
        || config->open_duration_ns == 0) {
        errno = EINVAL;
        return -1;
    }
    memset(breaker, 0, sizeof(*breaker));
    breaker->state = CIRCUIT_BREAKER_CLOSED;
    breaker->config = *config;
    return 0;
}

int circuit_breaker_tick(circuit_breaker_t *breaker, uint64_t now_ns)
{
    if (check_time(breaker, now_ns) != 0) {
        return -1;
    }
    if (breaker->state == CIRCUIT_BREAKER_OPEN
        && now_ns - breaker->opened_at_ns >= breaker->config.open_duration_ns) {
        breaker->state = CIRCUIT_BREAKER_HALF_OPEN;
        breaker->half_open_trial_in_flight = 0;
    }
    if (breaker->state == CIRCUIT_BREAKER_CLOSED) {
        purge_old(breaker, now_ns);
    }
    return 0;
}

int circuit_breaker_try_acquire(circuit_breaker_t *breaker, uint64_t now_ns)
{
    if (circuit_breaker_tick(breaker, now_ns) != 0) {
        return -1;
    }
    if (breaker->state == CIRCUIT_BREAKER_OPEN) {
        breaker->rejected_requests++;
        errno = EAGAIN;
        return 0;
    }
    if (breaker->state == CIRCUIT_BREAKER_HALF_OPEN) {
        if (breaker->half_open_trial_in_flight) {
            breaker->rejected_requests++;
            errno = EAGAIN;
            return 0;
        }
        breaker->half_open_trial_in_flight = 1;
    }
    return 1;
}

int circuit_breaker_record_success(circuit_breaker_t *breaker, uint64_t now_ns)
{
    if (check_time(breaker, now_ns) != 0) {
        return -1;
    }
    if (breaker->state == CIRCUIT_BREAKER_HALF_OPEN) {
        if (!breaker->half_open_trial_in_flight) {
            errno = EINVAL;
            return -1;
        }
        breaker->state = CIRCUIT_BREAKER_CLOSED;
        breaker->half_open_trial_in_flight = 0;
        breaker->failure_count = 0;
        breaker->closes++;
    } else if (breaker->state == CIRCUIT_BREAKER_CLOSED) {
        purge_old(breaker, now_ns);
    }
    return 0;
}

int circuit_breaker_record_failure(circuit_breaker_t *breaker, uint64_t now_ns)
{
    if (check_time(breaker, now_ns) != 0) {
        return -1;
    }
    if (breaker->state == CIRCUIT_BREAKER_HALF_OPEN) {
        if (!breaker->half_open_trial_in_flight) {
            errno = EINVAL;
            return -1;
        }
        open_breaker(breaker, now_ns);
        return 0;
    }
    if (breaker->state == CIRCUIT_BREAKER_OPEN) {
        return 0;
    }
    purge_old(breaker, now_ns);
    if (breaker->failure_count >= CIRCUIT_BREAKER_MAX_FAILURES) {
        errno = ENOSPC;
        return -1;
    }
    breaker->failure_times[breaker->failure_count++] = now_ns;
    if (breaker->failure_count >= breaker->config.failure_threshold) {
        open_breaker(breaker, now_ns);
    }
    return 0;
}

size_t circuit_breaker_recent_failures(circuit_breaker_t *breaker, uint64_t now_ns)
{
    if (circuit_breaker_tick(breaker, now_ns) != 0) {
        return 0;
    }
    return breaker->failure_count;
}

const char *circuit_breaker_state_name(circuit_breaker_state_t state)
{
    switch (state) {
    case CIRCUIT_BREAKER_CLOSED: return "CLOSED";
    case CIRCUIT_BREAKER_OPEN: return "OPEN";
    case CIRCUIT_BREAKER_HALF_OPEN: return "HALF_OPEN";
    default: return "INVALID";
    }
}
