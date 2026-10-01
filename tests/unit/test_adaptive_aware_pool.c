#include "adaptive_aware_pool.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_score_components(void)
{
    adaptive_score_config_t config = {1U, 2U, 80U};
    adaptive_score_result_t result;
    assert(adaptive_score_compute(2000U, 1, 2000U, 2U, 10U, &config, &result) == 0);
    assert(result.latency_component == 1000U);
    assert(result.load_component == 200U);
    assert(result.combined_score == 466U);
    assert(result.used_latency == 1);

    assert(adaptive_score_compute(3000U, 1, 2000U, 1U, 10U, &config, &result) == 0);
    assert(result.latency_component == 1500U);
    assert(result.load_component == 100U);
    assert(result.combined_score == 566U);

    assert(adaptive_score_compute(0U, 0, 0U, 3U, 10U, &config, &result) == 0);
    assert(result.used_latency == 0);
    assert(result.load_component == 300U);
    assert(result.combined_score == 300U);
}

static void test_invalid_score_config(void)
{
    adaptive_score_config_t zero = {0U, 0U, 0U};
    adaptive_score_config_t too_large = {ADAPTIVE_SCORE_MAX_WEIGHT + 1U, 1U, 0U};
    adaptive_score_result_t result;
    assert(adaptive_score_validate_config(&zero) == -1);
    assert(adaptive_score_validate_config(&too_large) == -1);
    assert(adaptive_score_compute(10U, 1, 0U, 0U, 1U, &too_large, &result) == -1);
}

static void init_common(backend_target_t *targets, backend_pool_t *pool,
    backend_health_t *health, circuit_breaker_t *breakers,
    edge_locality_t *localities, latency_signal_t *latencies,
    adaptive_aware_pool_t *selector, const adaptive_score_config_t *score_cfg)
{
    static const backend_health_config_t health_cfg = {3U, 2U, 100U};
    static const circuit_breaker_config_t breaker_cfg = {3U, 100U, 50U};
    static const latency_signal_config_t latency_cfg = {1U, 4U, 50U};

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
    assert(adaptive_aware_pool_init(selector, pool, health, breakers, localities,
        latencies, 3U, score_cfg) == 0);
}

static void test_load_shifts_equal_latency(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    adaptive_aware_pool_t selector; edge_locality_t request;
    adaptive_score_config_t cfg = {1U, 2U, 0U};
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, &cfg);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);
    targets[0].active_connections = 8U; targets[1].active_connections = 2U; targets[2].active_connections = 6U;
    for (size_t i = 0; i < 3U; ++i) assert(latency_signal_record(&latencies[i], 3000U, 10U) == 0);
    size_t index = 0; edge_locality_rank_t rank; adaptive_score_result_t score;
    const backend_target_t *t = adaptive_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &score);
    assert(t != NULL && strcmp(t->name, "b") == 0);
    assert(score.load_component == 200U);
    assert(backend_pool_release(&pool, index) == 0);
}

static void test_load_can_outweigh_modest_latency_advantage(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    adaptive_aware_pool_t selector; edge_locality_t request;
    adaptive_score_config_t cfg = {1U, 2U, 0U};
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, &cfg);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);
    targets[0].active_connections = 9U; targets[1].active_connections = 1U; targets[2].active_connections = 7U;
    assert(latency_signal_record(&latencies[0], 2000U, 10U) == 0);
    assert(latency_signal_record(&latencies[1], 3000U, 10U) == 0);
    assert(latency_signal_record(&latencies[2], 4000U, 10U) == 0);
    size_t index = 0; edge_locality_rank_t rank; adaptive_score_result_t score;
    const backend_target_t *t = adaptive_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &score);
    assert(t != NULL && strcmp(t->name, "b") == 0);
    assert(backend_pool_release(&pool, index) == 0);
}

static void test_score_slack_preserves_fairness(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    adaptive_aware_pool_t selector; edge_locality_t request;
    adaptive_score_config_t cfg = {1U, 2U, 80U};
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, &cfg);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);
    targets[0].active_connections = 2U; targets[1].active_connections = 2U; targets[2].active_connections = 8U;
    assert(latency_signal_record(&latencies[0], 2000U, 10U) == 0);
    assert(latency_signal_record(&latencies[1], 2100U, 10U) == 0);
    assert(latency_signal_record(&latencies[2], 5000U, 10U) == 0);
    size_t index = 0; edge_locality_rank_t rank; adaptive_score_result_t score;
    const backend_target_t *t = adaptive_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &score);
    assert(t != NULL && strcmp(t->name, "a") == 0); assert(backend_pool_release(&pool, index) == 0);
    t = adaptive_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &score);
    assert(t != NULL && strcmp(t->name, "b") == 0); assert(backend_pool_release(&pool, index) == 0);
}

static void test_locality_still_outranks_remote_score(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    adaptive_aware_pool_t selector; edge_locality_t request;
    adaptive_score_config_t cfg = {1U, 2U, 0U};
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, &cfg);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);
    assert(edge_locality_set(&localities[2], "r2", "z9", "s9") == 0);
    targets[0].active_connections = 8U; targets[1].active_connections = 8U; targets[2].active_connections = 0U;
    assert(latency_signal_record(&latencies[0], 10000U, 10U) == 0);
    assert(latency_signal_record(&latencies[1], 11000U, 10U) == 0);
    assert(latency_signal_record(&latencies[2], 1000U, 10U) == 0);
    size_t index = 0; edge_locality_rank_t rank; adaptive_score_result_t score;
    const backend_target_t *t = adaptive_aware_pool_acquire_next(&selector, &request, 20U, &index, &rank, &score);
    assert(t != NULL && strcmp(t->name, "a") == 0 && rank == EDGE_LOCALITY_SITE);
    assert(backend_pool_release(&pool, index) == 0);
}

static void test_stale_latency_uses_load_without_stale_bias(void)
{
    backend_target_t targets[3]; backend_pool_t pool; backend_health_t health[3];
    circuit_breaker_t breakers[3]; edge_locality_t localities[3]; latency_signal_t latencies[3];
    adaptive_aware_pool_t selector; edge_locality_t request;
    adaptive_score_config_t cfg = {1U, 2U, 0U};
    init_common(targets, &pool, health, breakers, localities, latencies, &selector, &cfg);
    assert(edge_locality_set(&request, "r1", "z1", "s1") == 0);
    targets[0].active_connections = 9U; targets[1].active_connections = 2U; targets[2].active_connections = 7U;
    assert(latency_signal_record(&latencies[0], 100U, 10U) == 0); /* stale at 100 */
    assert(latency_signal_record(&latencies[1], 800U, 90U) == 0);
    assert(latency_signal_record(&latencies[2], 1200U, 90U) == 0);
    size_t index = 0; edge_locality_rank_t rank; adaptive_score_result_t score;
    const backend_target_t *t = adaptive_aware_pool_acquire_next(&selector, &request, 100U, &index, &rank, &score);
    assert(t != NULL && strcmp(t->name, "b") == 0);
    assert(backend_pool_release(&pool, index) == 0);
}

int main(void)
{
    test_score_components();
    test_invalid_score_config();
    test_load_shifts_equal_latency();
    test_load_can_outweigh_modest_latency_advantage();
    test_score_slack_preserves_fairness();
    test_locality_still_outranks_remote_score();
    test_stale_latency_uses_load_without_stale_bias();
    puts("adaptive load/latency scoring tests passed");
    return 0;
}
