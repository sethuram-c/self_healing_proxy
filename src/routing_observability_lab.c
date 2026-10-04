#define _POSIX_C_SOURCE 200809L
#include "adaptive_aware_pool.h"
#include "routing_decision.h"
#include "routing_perf.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BACKEND_COUNT 4U
#define PERF_OPS 100000U
#define PERF_ROUNDS 5U

static const backend_health_config_t HEALTH_CFG = {3U, 2U, 100U};
static const circuit_breaker_config_t BREAKER_CFG = {3U, 100U, 50U};
static const latency_signal_config_t LATENCY_CFG = {1U, 4U, 50U};
static const adaptive_score_config_t SCORE_CFG = {1U, 2U, 80U};

typedef struct {
    backend_target_t targets[BACKEND_COUNT];
    backend_pool_t pool;
    backend_health_t health[BACKEND_COUNT];
    circuit_breaker_t breakers[BACKEND_COUNT];
    edge_locality_t localities[BACKEND_COUNT];
    latency_signal_t latencies[BACKEND_COUNT];
    adaptive_aware_pool_t selector;
    edge_locality_t request;
} fixture_t;

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int fixture_init(fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    f->targets[0] = (backend_target_t){"edge-a", "127.0.0.1", 9101U, 2U, 10U};
    f->targets[1] = (backend_target_t){"edge-b", "127.0.0.1", 9102U, 2U, 10U};
    f->targets[2] = (backend_target_t){"zone-c", "127.0.0.1", 9103U, 1U, 10U};
    f->targets[3] = (backend_target_t){"remote-d", "127.0.0.1", 9104U, 0U, 10U};
    if (backend_pool_init(&f->pool, f->targets, BACKEND_COUNT) != 0) return -1;
    for (size_t i = 0; i < BACKEND_COUNT; ++i) {
        if (backend_health_init(&f->health[i], &HEALTH_CFG) != 0) return -1;
        if (backend_health_record_success(&f->health[i], 1U) != 0) return -1;
        if (circuit_breaker_init(&f->breakers[i], &BREAKER_CFG) != 0) return -1;
        if (latency_signal_init(&f->latencies[i], &LATENCY_CFG) != 0) return -1;
    }
    if (edge_locality_set(&f->request, "r1", "z1", "s1") != 0) return -1;
    if (edge_locality_set(&f->localities[0], "r1", "z1", "s1") != 0) return -1;
    if (edge_locality_set(&f->localities[1], "r1", "z1", "s1") != 0) return -1;
    if (edge_locality_set(&f->localities[2], "r1", "z1", "s2") != 0) return -1;
    if (edge_locality_set(&f->localities[3], "r2", "z9", "s9") != 0) return -1;
    if (latency_signal_record(&f->latencies[0], UINT64_C(2000000), 10U) != 0) return -1;
    if (latency_signal_record(&f->latencies[1], UINT64_C(2100000), 10U) != 0) return -1;
    if (latency_signal_record(&f->latencies[2], UINT64_C(3000000), 10U) != 0) return -1;
    if (latency_signal_record(&f->latencies[3], UINT64_C(1000000), 10U) != 0) return -1;
    return adaptive_aware_pool_init(&f->selector, &f->pool, f->health, f->breakers,
        f->localities, f->latencies, BACKEND_COUNT, &SCORE_CFG);
}

static int emit_decision(fixture_t *f, uint64_t now_ns, uint64_t sequence)
{
    adaptive_selection_trace_t trace;
    const backend_target_t *target = adaptive_aware_pool_acquire_next_traced(
        &f->selector, &f->request, now_ns, &trace);
    if (target == NULL) return -1;
    routing_decision_event_t event;
    if (routing_decision_event_init(&event, now_ns, sequence, trace.selected_index,
            target->name, trace.selected_rank, trace.fastest_fresh_latency_ns,
            trace.fresh_latency_candidates, trace.safe_locality_candidates,
            trace.best_score, trace.score_threshold, &trace.selected_score) != 0) return -1;
    char json[1024];
    if (routing_decision_serialize_json(&event, json, sizeof(json)) != 0) return -1;
    puts(json);
    return backend_pool_release(&f->pool, trace.selected_index);
}

static int scenario_selected(void)
{
    fixture_t f;
    if (fixture_init(&f) != 0) return 1;
    if (emit_decision(&f, 20U, 1U) != 0) return 1;
    puts("PHASE54_ROUTING_OBSERVABILITY_OK");
    return 0;
}

static int make_unhealthy(backend_health_t *h, uint64_t start)
{
    return backend_health_record_failure(h, start) == 0
        && backend_health_record_failure(h, start + 1U) == 0
        && backend_health_record_failure(h, start + 2U) == 0 ? 0 : -1;
}

static int scenario_fallback(void)
{
    fixture_t f;
    if (fixture_init(&f) != 0) return 1;
    if (make_unhealthy(&f.health[0], 20U) != 0 || make_unhealthy(&f.health[1], 20U) != 0) return 1;
    if (emit_decision(&f, 30U, 7U) != 0) return 1;
    puts("PHASE54_ROUTING_OBSERVABILITY_OK");
    return 0;
}

static int scenario_load_only(void)
{
    fixture_t f;
    if (fixture_init(&f) != 0) return 1;
    /* All SITE samples are stale at t=100; selector should expose used_latency=false. */
    if (emit_decision(&f, 100U, 9U) != 0) return 1;
    puts("PHASE54_ROUTING_OBSERVABILITY_OK");
    return 0;
}

static int bench_one(int traced, uint64_t *ns_per_op_x1000, uint64_t *checksum)
{
    routing_perf_samples_t samples = {0};
    uint64_t sum = 0;
    for (size_t round = 0; round < PERF_ROUNDS; ++round) {
        fixture_t f;
        if (fixture_init(&f) != 0) return -1;
        uint64_t start = monotonic_ns();
        if (start == 0) return -1;
        for (size_t i = 0; i < PERF_OPS; ++i) {
            size_t index = 0;
            if (traced) {
                adaptive_selection_trace_t trace;
                const backend_target_t *t = adaptive_aware_pool_acquire_next_traced(
                    &f.selector, &f.request, 20U, &trace);
                if (t == NULL) return -1;
                index = trace.selected_index;
                sum += trace.selected_score.combined_score + index;
            } else {
                edge_locality_rank_t rank;
                adaptive_score_result_t score;
                const backend_target_t *t = adaptive_aware_pool_acquire_next(
                    &f.selector, &f.request, 20U, &index, &rank, &score);
                if (t == NULL) return -1;
                sum += score.combined_score + index + (uint64_t)rank;
            }
            if (backend_pool_release(&f.pool, index) != 0) return -1;
        }
        uint64_t end = monotonic_ns();
        if (end <= start || routing_perf_add_sample(&samples, end - start) != 0) return -1;
    }
    uint64_t median;
    if (routing_perf_median(&samples, &median) != 0) return -1;
    *ns_per_op_x1000 = routing_perf_ns_per_op_x1000(median, PERF_OPS);
    *checksum = sum;
    return 0;
}

static int scenario_overhead(void)
{
    uint64_t plain = 0, traced = 0, a = 0, b = 0;
    if (bench_one(0, &plain, &a) != 0 || bench_one(1, &traced, &b) != 0) return 1;
    uint64_t ratio = routing_perf_ratio_x1000(traced, plain);
    printf("UNTRACED_NS_PER_OP_X1000=%" PRIu64 "\n", plain);
    printf("TRACED_NS_PER_OP_X1000=%" PRIu64 "\n", traced);
    printf("TRACE_OVERHEAD_X1000=%" PRIu64 "\n", ratio);
    printf("TRACE_BUDGET_X1000=3000\n");
    printf("TRACE_ABSOLUTE_BUDGET_NS=10000\n");
    printf("CHECKSUM=%" PRIu64 "\n", a + b);
    if (ratio > 3000U || traced > UINT64_C(10000000)) return 1;
    puts("PHASE54_ROUTING_OBSERVABILITY_OK");
    return 0;
}

static void usage(const char *p)
{
    fprintf(stderr, "Usage: %s --scenario selected|fallback|load-only|overhead\n", p);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(argv[0]); return 0; }
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) { usage(argv[0]); return 2; }
    if (strcmp(argv[2], "selected") == 0) return scenario_selected();
    if (strcmp(argv[2], "fallback") == 0) return scenario_fallback();
    if (strcmp(argv[2], "load-only") == 0) return scenario_load_only();
    if (strcmp(argv[2], "overhead") == 0) return scenario_overhead();
    usage(argv[0]);
    return 2;
}
