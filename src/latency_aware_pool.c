#include "latency_aware_pool.h"

#include <stddef.h>
#include <stdint.h>

static int circuit_can_attempt(circuit_breaker_t *breaker, uint64_t now_ns)
{
    if (circuit_breaker_tick(breaker, now_ns) != 0) {
        return 0;
    }
    if (breaker->state == CIRCUIT_BREAKER_CLOSED) {
        return 1;
    }
    return breaker->state == CIRCUIT_BREAKER_HALF_OPEN && !breaker->half_open_trial_in_flight;
}

static int base_candidate(latency_aware_pool_t *selector, size_t index,
    const edge_locality_t *request_locality, uint64_t now_ns,
    edge_locality_rank_t required_rank, int enforce_rank)
{
    backend_target_t *target = &selector->pool->targets[index];
    if (!backend_health_is_eligible(&selector->health[index])
        || target->active_connections >= target->max_connections
        || !circuit_can_attempt(&selector->breakers[index], now_ns)) {
        return 0;
    }
    if (enforce_rank
        && edge_locality_rank(request_locality, &selector->localities[index]) != required_rank) {
        return 0;
    }
    return 1;
}

int latency_aware_pool_init(latency_aware_pool_t *selector, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers,
    edge_locality_t *localities, latency_signal_t *latencies, size_t count,
    const latency_aware_pool_config_t *config)
{
    if (selector == NULL || pool == NULL || pool->targets == NULL || pool->count == 0
        || health == NULL || breakers == NULL || localities == NULL || latencies == NULL
        || config == NULL || count != pool->count) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (edge_locality_validate(&localities[i]) != 0) {
            return -1;
        }
    }
    selector->pool = pool;
    selector->health = health;
    selector->breakers = breakers;
    selector->localities = localities;
    selector->latencies = latencies;
    selector->count = count;
    selector->config = *config;
    return 0;
}

const backend_target_t *latency_aware_pool_acquire_next(latency_aware_pool_t *selector,
    const edge_locality_t *request_locality, uint64_t now_ns,
    size_t *selected_index, edge_locality_rank_t *selected_rank,
    uint64_t *selected_latency_ns)
{
    if (selector == NULL || selector->pool == NULL || request_locality == NULL
        || edge_locality_validate(request_locality) != 0
        || selector->count != selector->pool->count) {
        return NULL;
    }

    backend_pool_t *pool = selector->pool;
    edge_locality_rank_t best_rank = EDGE_LOCALITY_REMOTE;
    int found_rank = 0;

    for (size_t i = 0; i < pool->count; ++i) {
        if (!base_candidate(selector, i, request_locality, now_ns, EDGE_LOCALITY_REMOTE, 0)) {
            continue;
        }
        edge_locality_rank_t rank = edge_locality_rank(request_locality, &selector->localities[i]);
        if (!found_rank || rank < best_rank) {
            best_rank = rank;
            found_rank = 1;
        }
    }
    if (!found_rank) {
        return NULL;
    }

    uint64_t fastest = 0;
    int have_fresh_latency = 0;
    for (size_t i = 0; i < pool->count; ++i) {
        if (!base_candidate(selector, i, request_locality, now_ns, best_rank, 1)) {
            continue;
        }
        uint64_t value = 0;
        int status = latency_signal_get(&selector->latencies[i], now_ns, &value);
        if (status < 0) {
            return NULL;
        }
        if (status > 0 && (!have_fresh_latency || value < fastest)) {
            fastest = value;
            have_fresh_latency = 1;
        }
    }

    uint64_t threshold = UINT64_MAX;
    if (have_fresh_latency && UINT64_MAX - fastest >= selector->config.latency_slack_ns) {
        threshold = fastest + selector->config.latency_slack_ns;
    }

    for (size_t offset = 0; offset < pool->count; ++offset) {
        size_t index = (pool->next_index + offset) % pool->count;
        if (!base_candidate(selector, index, request_locality, now_ns, best_rank, 1)) {
            continue;
        }

        uint64_t latency = 0;
        if (have_fresh_latency) {
            int status = latency_signal_get(&selector->latencies[index], now_ns, &latency);
            if (status <= 0 || latency > threshold) {
                continue;
            }
        }

        if (circuit_breaker_try_acquire(&selector->breakers[index], now_ns) <= 0) {
            continue;
        }
        pool->targets[index].active_connections++;
        pool->next_index = (index + 1U) % pool->count;
        if (selected_index != NULL) *selected_index = index;
        if (selected_rank != NULL) *selected_rank = best_rank;
        if (selected_latency_ns != NULL) *selected_latency_ns = have_fresh_latency ? latency : 0;
        return &pool->targets[index];
    }
    return NULL;
}
