#include "adaptive_aware_pool.h"
#include "routing_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s --scenario defaults|show|decision [--config PATH]\n", prog);
}

static void print_policy(const routing_policy_t *policy)
{
    printf("ALPHA=%u/%u STALE_MS=%llu LATENCY_WEIGHT=%u LOAD_WEIGHT=%u SCORE_SLACK=%u\n",
        policy->latency.alpha_numerator, policy->latency.alpha_denominator,
        (unsigned long long)routing_policy_stale_after_ms(policy),
        policy->score.latency_weight, policy->score.load_weight,
        policy->score.score_slack);
}

static int init_fixture(const routing_policy_t *policy, backend_pool_t *pool,
    backend_target_t targets[2], backend_health_t health[2],
    circuit_breaker_t breakers[2], edge_locality_t localities[2],
    latency_signal_t latencies[2], adaptive_aware_pool_t *selector)
{
    targets[0] = (backend_target_t){.name = "edge-fast-hot", .ip = "127.0.0.1", .port = 9101,
        .max_connections = 10, .active_connections = 9};
    targets[1] = (backend_target_t){.name = "edge-slower-idle", .ip = "127.0.0.1", .port = 9102,
        .max_connections = 10, .active_connections = 1};
    *pool = (backend_pool_t){.targets = targets, .count = 2, .next_index = 0};

    backend_health_config_t health_cfg = {3U, 2U, 100U};
    circuit_breaker_config_t breaker_cfg = {3U, 100U, 50U};
    for (size_t i = 0; i < 2; ++i) {
        if (backend_health_init(&health[i], &health_cfg) != 0
            || circuit_breaker_init(&breakers[i], &breaker_cfg) != 0
            || edge_locality_set(&localities[i], "r1", "z1", "s1") != 0) {
            return -1;
        }
        health[i].state = BACKEND_HEALTH_HEALTHY;
    }
    if (routing_policy_init_latency_signals(policy, latencies, 2U) != 0
        || latency_signal_record(&latencies[0], 2000000ULL, 1000000000ULL) != 0
        || latency_signal_record(&latencies[1], 3000000ULL, 1000000000ULL) != 0
        || adaptive_aware_pool_init(selector, pool, health, breakers, localities,
            latencies, 2U, &policy->score) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *scenario = NULL;
    const char *config_path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
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

    routing_policy_t policy;
    routing_policy_defaults(&policy);
    if (config_path != NULL && routing_policy_load_file(config_path, &policy) != 0) {
        fprintf(stderr, "invalid routing policy: %s\n", config_path);
        return 1;
    }

    if (strcmp(scenario, "defaults") == 0 || strcmp(scenario, "show") == 0) {
        print_policy(&policy);
        puts("PHASE57_ROUTING_POLICY_OK");
        return 0;
    }
    if (strcmp(scenario, "decision") != 0) {
        usage(argv[0]);
        return 2;
    }

    backend_pool_t pool;
    backend_target_t targets[2];
    backend_health_t health[2];
    circuit_breaker_t breakers[2];
    edge_locality_t localities[2];
    latency_signal_t latencies[2];
    adaptive_aware_pool_t selector;
    if (init_fixture(&policy, &pool, targets, health, breakers, localities,
            latencies, &selector) != 0) {
        return 1;
    }

    edge_locality_t request;
    if (edge_locality_set(&request, "r1", "z1", "s1") != 0) return 1;
    size_t index = 0;
    edge_locality_rank_t rank = EDGE_LOCALITY_REMOTE;
    adaptive_score_result_t score;
    const backend_target_t *selected = adaptive_aware_pool_acquire_next(&selector,
        &request, 1000000100ULL, &index, &rank, &score);
    if (selected == NULL) return 1;

    print_policy(&policy);
    printf("SELECTED=%s RANK=%s SCORE=%u USED_LATENCY=%d\n",
        selected->name, edge_locality_rank_name(rank), score.combined_score,
        score.used_latency);
    puts("PHASE57_ROUTING_POLICY_OK");
    return 0;
}
