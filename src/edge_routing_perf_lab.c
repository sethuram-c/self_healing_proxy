#define _POSIX_C_SOURCE 200809L

#include "adaptive_aware_pool.h"
#include "edge_aware_pool.h"
#include "routing_perf.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PERF_MAX_BACKENDS 32U
#define PERF_COMPARE_BACKENDS 8U
#define PERF_COMPARE_ITERS 200000U
#define PERF_COMPARE_ROUNDS 7U
#define PERF_SCALE_ITERS 100000U
#define PERF_SCALE_ROUNDS 5U
#define PERF_WARMUP_ITERS 20000U
#define PERF_RATIO_BUDGET_X1000 10000U
#define PERF_ABSOLUTE_BUDGET_NS 20000U
#define PERF_SCALE_GROWTH_BUDGET_X1000 14000U
#define PERF_SCALE_32_ABSOLUTE_BUDGET_NS 60000U

static volatile uint64_t perf_sink;

typedef struct {
    backend_target_t targets[PERF_MAX_BACKENDS];
    backend_pool_t pool;
    backend_health_t health[PERF_MAX_BACKENDS];
    circuit_breaker_t breakers[PERF_MAX_BACKENDS];
    edge_locality_t localities[PERF_MAX_BACKENDS];
    latency_signal_t latencies[PERF_MAX_BACKENDS];
    edge_aware_pool_t edge;
    adaptive_aware_pool_t adaptive;
    edge_locality_t request_locality;
    size_t count;
} perf_fixture_t;

static uint64_t diff_ns(const struct timespec *start, const struct timespec *end)
{
    uint64_t sec = (uint64_t)(end->tv_sec - start->tv_sec);
    int64_t nsec = end->tv_nsec - start->tv_nsec;
    if (nsec < 0) {
        sec--;
        nsec += 1000000000LL;
    }
    return sec * 1000000000ULL + (uint64_t)nsec;
}

static int fixture_init(perf_fixture_t *fixture, size_t count)
{
    if (fixture == NULL || count == 0 || count > PERF_MAX_BACKENDS) {
        errno = EINVAL;
        return -1;
    }
    memset(fixture, 0, sizeof(*fixture));
    fixture->count = count;

    backend_health_config_t health_cfg = {3, 2, 100};
    circuit_breaker_config_t circuit_cfg = {3, 100, 50};
    latency_signal_config_t latency_cfg = {1, 4, 1000000000ULL};
    adaptive_score_config_t score_cfg = {1, 2, 80};

    if (edge_locality_set(&fixture->request_locality, "r1", "z1", "s1") != 0) {
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        fixture->targets[i].name = "backend";
        fixture->targets[i].ip = "127.0.0.1";
        fixture->targets[i].port = (uint16_t)(9000U + i);
        fixture->targets[i].active_connections = i % 4U;
        fixture->targets[i].max_connections = 64;
        if (backend_health_init(&fixture->health[i], &health_cfg) != 0
            || backend_health_record_success(&fixture->health[i], 1) != 0
            || circuit_breaker_init(&fixture->breakers[i], &circuit_cfg) != 0
            || edge_locality_set(&fixture->localities[i], "r1", "z1", "s1") != 0
            || latency_signal_init(&fixture->latencies[i], &latency_cfg) != 0
            || latency_signal_record(&fixture->latencies[i],
                2000000ULL + (uint64_t)(i % 4U) * 100000ULL, 1) != 0) {
            return -1;
        }
    }
    if (backend_pool_init(&fixture->pool, fixture->targets, count) != 0) {
        return -1;
    }
    if (edge_aware_pool_init(&fixture->edge, &fixture->pool,
            fixture->health, fixture->breakers, fixture->localities, count) != 0) {
        return -1;
    }
    if (adaptive_aware_pool_init(&fixture->adaptive, &fixture->pool,
            fixture->health, fixture->breakers, fixture->localities,
            fixture->latencies, count, &score_cfg) != 0) {
        return -1;
    }
    return 0;
}

static int run_iterations(perf_fixture_t *fixture, uint64_t iterations,
    int adaptive_mode, uint64_t *checksum)
{
    uint64_t sum = 0;
    for (uint64_t i = 0; i < iterations; ++i) {
        size_t selected = 0;
        const backend_target_t *target;
        if (adaptive_mode) {
            adaptive_score_result_t score = {0};
            target = adaptive_aware_pool_acquire_next(&fixture->adaptive,
                &fixture->request_locality, 100, &selected, NULL, &score);
            sum += score.combined_score;
        } else {
            target = edge_aware_pool_acquire_next(&fixture->edge,
                &fixture->request_locality, 100, &selected, NULL);
        }
        if (target == NULL || backend_pool_release(&fixture->pool, selected) != 0) {
            return -1;
        }
        sum += (uint64_t)selected + 1U;
    }
    perf_sink += sum;
    if (checksum != NULL) {
        *checksum += sum;
    }
    return 0;
}

static int measure_mode(size_t backends, uint64_t iterations, size_t rounds,
    int adaptive_mode, uint64_t *median_ns_per_op_x1000, uint64_t *checksum)
{
    if (median_ns_per_op_x1000 == NULL || rounds == 0
        || rounds > ROUTING_PERF_MAX_SAMPLES) {
        return -1;
    }

    routing_perf_samples_t samples = {0};
    perf_fixture_t fixture;
    if (fixture_init(&fixture, backends) != 0) {
        return -1;
    }
    if (run_iterations(&fixture, PERF_WARMUP_ITERS, adaptive_mode, checksum) != 0) {
        return -1;
    }

    for (size_t round = 0; round < rounds; ++round) {
        struct timespec start;
        struct timespec end;
        if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
            return -1;
        }
        if (run_iterations(&fixture, iterations, adaptive_mode, checksum) != 0) {
            return -1;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
            return -1;
        }
        uint64_t elapsed = diff_ns(&start, &end);
        if (routing_perf_add_sample(&samples, elapsed) != 0) {
            return -1;
        }
    }

    uint64_t median_elapsed = 0;
    if (routing_perf_median(&samples, &median_elapsed) != 0) {
        return -1;
    }
    *median_ns_per_op_x1000 = routing_perf_ns_per_op_x1000(median_elapsed, iterations);
    return 0;
}

static void print_ns_x1000(const char *name, uint64_t value)
{
    printf("%s=%" PRIu64 ".%03" PRIu64, name, value / 1000U, value % 1000U);
}

static int scenario_compare(void)
{
    uint64_t baseline = 0;
    uint64_t adaptive = 0;
    uint64_t checksum = 0;
    if (measure_mode(PERF_COMPARE_BACKENDS, PERF_COMPARE_ITERS,
            PERF_COMPARE_ROUNDS, 0, &baseline, &checksum) != 0
        || measure_mode(PERF_COMPARE_BACKENDS, PERF_COMPARE_ITERS,
            PERF_COMPARE_ROUNDS, 1, &adaptive, &checksum) != 0) {
        return 1;
    }
    uint64_t ratio = routing_perf_ratio_x1000(adaptive, baseline);
    print_ns_x1000("BASELINE_NS_PER_OP", baseline);
    putchar(' ');
    print_ns_x1000("ADAPTIVE_NS_PER_OP", adaptive);
    printf(" OVERHEAD_X1000=%" PRIu64 " BUDGET_X1000=%u ABSOLUTE_BUDGET_NS=%u CHECKSUM=%" PRIu64 "\n",
        ratio, PERF_RATIO_BUDGET_X1000, PERF_ABSOLUTE_BUDGET_NS, checksum);
    if (ratio > PERF_RATIO_BUDGET_X1000
        || adaptive > (uint64_t)PERF_ABSOLUTE_BUDGET_NS * 1000U) {
        fprintf(stderr, "Phase 53 compare budget exceeded\n");
        return 1;
    }
    puts("PHASE53_EDGE_ROUTING_PERF_OK");
    return 0;
}

static int scenario_scale(void)
{
    uint64_t n4 = 0;
    uint64_t n16 = 0;
    uint64_t n32 = 0;
    uint64_t checksum = 0;
    if (measure_mode(4, PERF_SCALE_ITERS, PERF_SCALE_ROUNDS, 1, &n4, &checksum) != 0
        || measure_mode(16, PERF_SCALE_ITERS, PERF_SCALE_ROUNDS, 1, &n16, &checksum) != 0
        || measure_mode(32, PERF_SCALE_ITERS, PERF_SCALE_ROUNDS, 1, &n32, &checksum) != 0) {
        return 1;
    }
    uint64_t growth = routing_perf_ratio_x1000(n32, n4);
    print_ns_x1000("N4_NS_PER_OP", n4);
    putchar(' ');
    print_ns_x1000("N16_NS_PER_OP", n16);
    putchar(' ');
    print_ns_x1000("N32_NS_PER_OP", n32);
    printf(" GROWTH_X1000=%" PRIu64 " GROWTH_BUDGET_X1000=%u N32_ABSOLUTE_BUDGET_NS=%u CHECKSUM=%" PRIu64 "\n",
        growth, PERF_SCALE_GROWTH_BUDGET_X1000,
        PERF_SCALE_32_ABSOLUTE_BUDGET_NS, checksum);
    if (growth > PERF_SCALE_GROWTH_BUDGET_X1000
        || n32 > (uint64_t)PERF_SCALE_32_ABSOLUTE_BUDGET_NS * 1000U) {
        fprintf(stderr, "Phase 53 scale budget exceeded\n");
        return 1;
    }
    puts("PHASE53_EDGE_ROUTING_PERF_OK");
    return 0;
}

static int scenario_smoke(void)
{
    perf_fixture_t fixture;
    uint64_t checksum = 0;
    if (fixture_init(&fixture, 8) != 0
        || run_iterations(&fixture, 5000, 1, &checksum) != 0) {
        return 1;
    }
    printf("SMOKE_OPS=5000 CHECKSUM=%" PRIu64 "\n", checksum);
    puts("PHASE53_EDGE_ROUTING_PERF_OK");
    return 0;
}

static void usage(const char *argv0)
{
    printf("Usage: %s --scenario compare|scale|smoke\n", argv0);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "compare") == 0) {
        return scenario_compare();
    }
    if (strcmp(argv[2], "scale") == 0) {
        return scenario_scale();
    }
    if (strcmp(argv[2], "smoke") == 0) {
        return scenario_smoke();
    }
    usage(argv[0]);
    return 2;
}
