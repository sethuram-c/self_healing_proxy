#include "latency_aware_pool.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_latency_signal_ewma_and_freshness(void)
{
    latency_signal_t signal;
    latency_signal_config_t config = {1U, 4U, 50U};
    uint64_t value = 0;
    assert(latency_signal_init(&signal, &config) == 0);
    assert(latency_signal_get(&signal, 0U, &value) == 0);
    assert(latency_signal_record(&signal, 100U, 10U) == 0);
    assert(signal.ewma_ns == 100U);
    assert(latency_signal_record(&signal, 60U, 20U) == 0);
    assert(signal.ewma_ns == 90U);
    assert(latency_signal_record(&signal, 130U, 30U) == 0);
    assert(signal.ewma_ns == 100U);
    assert(latency_signal_get(&signal, 80U, &value) == 1 && value == 100U);
    assert(latency_signal_get(&signal, 81U, &value) == 0);
    assert(latency_signal_record(&signal, 10U, 29U) == -1);
}

static void test_invalid_latency_config(void)
{
    latency_signal_t signal;
    latency_signal_config_t zero_alpha = {0U, 4U, 50U};
    latency_signal_config_t bad_ratio = {5U, 4U, 50U};
    latency_signal_config_t zero_stale = {1U, 4U, 0U};
    assert(latency_signal_init(&signal, &zero_alpha) == -1);
    assert(latency_signal_init(&signal, &bad_ratio) == -1);
    assert(latency_signal_init(&signal, &zero_stale) == -1);
}

static void init_common(backend_target_t *targets, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers,
    edge_locality_t *localities, latency_signal_t *latencies,
    latency_aware_pool_t *selector, uint64_t slack)
{
    static const backend_health_config_t health_cfg = {3U, 2U, 100U};
    static const circuit_breaker_config_t breaker_cfg = {3U, 100U, 50U};
    static const latency_signal_config_t latency_cfg = {1U, 4U, 50U};
    latency_aware_pool_config_t selector_cfg = {slack};

    targets[0] = (backend_target_t){"a", "127.0.0.1", 1U, 0U, 10U};
    targets[1] = (backend_target_t){"b", "127.0.0.1", 2U, 0U, 10U};
    targets[2] = (backend_target_t){"c", "127.0.0.1", 3U, 0U, 10U};
    assert(backend_pool_init(pool, targets, 3U) == 0);
    for (size_t i = 0; i < 3U; ++i) {
        assert(backend_health_init(&health[i], &health_cfg) == 0);
        assert(backend_health_record_success(&health[i], 1U) == 0);
        assert(circuit_breaker_init(&breakers[i], &breaker_cfg) == 0);
        assert(edge_locality_set(&localities[i], "r1", "z1", "s1") == 0);
        assert(latency_signal_init(&latencies[i], &latency_cfg) == 0);
    }
    assert(latency_aware_pool_init(selector, pool, health, breakers, localities,
        latencies, 3U, &selector_cfg) == 0);
}

static void test_latency_slack_preserves_fairness(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    latency_aware_pool_t selector; edge_locality_t request;
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, 500U);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);
    assert(latency_signal_record(&latencies[0], 5000U, 10U) == 0);
    assert(latency_signal_record(&latencies[1], 2000U, 10U) == 0);
    assert(latency_signal_record(&latencies[2], 2300U, 10U) == 0);

    size_t index = 0; edge_locality_rank_t rank; uint64_t latency = 0;
    const backend_target_t *t = latency_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &latency);
    assert(t != NULL && strcmp(t->name, "b") == 0 && latency == 2000U);
    assert(backend_pool_release(&pool, index) == 0);
    t = latency_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &latency);
    assert(t != NULL && strcmp(t->name, "c") == 0 && latency == 2300U);
    assert(backend_pool_release(&pool, index) == 0);
}

static void test_stale_fast_sample_loses_authority(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    latency_aware_pool_t selector; edge_locality_t request;
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, 0U);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);
    assert(latency_signal_record(&latencies[0], 100U, 10U) == 0);
    assert(latency_signal_record(&latencies[1], 800U, 90U) == 0);
    assert(latency_signal_record(&latencies[2], 1200U, 90U) == 0);

    size_t index = 0; edge_locality_rank_t rank; uint64_t latency = 0;
    const backend_target_t *t = latency_aware_pool_acquire_next(&selector, &request, 100U, &index, &rank, &latency);
    assert(t != NULL && strcmp(t->name, "b") == 0 && latency == 800U);
    assert(backend_pool_release(&pool, index) == 0);
}

static void test_locality_outranks_remote_latency(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    latency_aware_pool_t selector; edge_locality_t request;
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, 0U);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);
    assert(edge_locality_set(&localities[2], "r2", "z9", "s9") == 0);
    assert(latency_signal_record(&latencies[0], 10000U, 10U) == 0);
    assert(latency_signal_record(&latencies[1], 11000U, 10U) == 0);
    assert(latency_signal_record(&latencies[2], 1U, 10U) == 0);

    size_t index = 0; edge_locality_rank_t rank; uint64_t latency = 0;
    const backend_target_t *t = latency_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &latency);
    assert(t != NULL && strcmp(t->name, "a") == 0);
    assert(rank == EDGE_LOCALITY_SITE);
    assert(backend_pool_release(&pool, index) == 0);
}

static void test_no_samples_falls_back_to_round_robin(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    latency_aware_pool_t selector; edge_locality_t request;
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, 0U);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);

    size_t index = 0; edge_locality_rank_t rank; uint64_t latency = 99U;
    const backend_target_t *t = latency_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &latency);
    assert(t != NULL && strcmp(t->name, "a") == 0 && latency == 0U);
    assert(backend_pool_release(&pool, index) == 0);
    t = latency_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &latency);
    assert(t != NULL && strcmp(t->name, "b") == 0 && latency == 0U);
    assert(backend_pool_release(&pool, index) == 0);
}

int main(void)
{
    test_latency_signal_ewma_and_freshness();
    test_invalid_latency_config();
    test_latency_slack_preserves_fairness();
    test_stale_fast_sample_loses_authority();
    test_locality_outranks_remote_latency();
    test_no_samples_falls_back_to_round_robin();
    puts("latency-aware routing tests passed");
    return 0;
}
