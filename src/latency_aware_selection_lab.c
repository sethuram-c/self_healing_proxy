#include "latency_aware_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BACKEND_COUNT 4U

static const backend_health_config_t HEALTH_CFG = {3U, 2U, 100U};
static const circuit_breaker_config_t BREAKER_CFG = {3U, 100U, 50U};
static const latency_signal_config_t LATENCY_CFG = {1U, 4U, 50U};
static const latency_aware_pool_config_t SELECTOR_CFG = {500000U};

typedef struct {
    backend_target_t targets[BACKEND_COUNT];
    backend_pool_t pool;
    backend_health_t health[BACKEND_COUNT];
    circuit_breaker_t breakers[BACKEND_COUNT];
    edge_locality_t localities[BACKEND_COUNT];
    latency_signal_t latencies[BACKEND_COUNT];
    latency_aware_pool_t selector;
    edge_locality_t request;
} fixture_t;

static int fixture_init(fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    f->targets[0] = (backend_target_t){"edge-a", "127.0.0.1", 9101U, 0U, 10U};
    f->targets[1] = (backend_target_t){"edge-b", "127.0.0.1", 9102U, 0U, 10U};
    f->targets[2] = (backend_target_t){"edge-c", "127.0.0.1", 9103U, 0U, 10U};
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
    if (edge_locality_set(&f->localities[2], "r1", "z1", "s1") != 0) return -1;
    if (edge_locality_set(&f->localities[3], "r2", "z9", "s9") != 0) return -1;

    return latency_aware_pool_init(&f->selector, &f->pool, f->health, f->breakers,
        f->localities, f->latencies, BACKEND_COUNT, &SELECTOR_CFG);
}

static int release_selected(fixture_t *f, size_t index)
{
    return backend_pool_release(&f->pool, index);
}

static int scenario_faster(void)
{
    fixture_t f;
    if (fixture_init(&f) != 0) return 1;
    if (latency_signal_record(&f.latencies[0], 5000000U, 10U) != 0) return 1;
    if (latency_signal_record(&f.latencies[1], 2000000U, 10U) != 0) return 1;
    if (latency_signal_record(&f.latencies[2], 2300000U, 10U) != 0) return 1;

    for (size_t n = 0; n < 4U; ++n) {
        size_t index = 0;
        edge_locality_rank_t rank;
        uint64_t latency = 0;
        const backend_target_t *target = latency_aware_pool_acquire_next(&f.selector,
            &f.request, 20U, &index, &rank, &latency);
        if (target == NULL) return 1;
        printf("PICK=%s RANK=%s EWMA_NS=%llu\n", target->name,
            edge_locality_rank_name(rank), (unsigned long long)latency);
        if (release_selected(&f, index) != 0) return 1;
    }
    puts("PHASE50_LATENCY_AWARE_SELECTION_OK");
    return 0;
}

static int scenario_stale(void)
{
    fixture_t f;
    if (fixture_init(&f) != 0) return 1;
    if (latency_signal_record(&f.latencies[0], 1000000U, 10U) != 0) return 1;
    if (latency_signal_record(&f.latencies[1], 8000000U, 90U) != 0) return 1;
    if (latency_signal_record(&f.latencies[2], 12000000U, 90U) != 0) return 1;

    size_t index = 0;
    edge_locality_rank_t rank;
    uint64_t latency = 0;
    const backend_target_t *target = latency_aware_pool_acquire_next(&f.selector,
        &f.request, 100U, &index, &rank, &latency);
    if (target == NULL) return 1;
    printf("SELECTED=%s RANK=%s EWMA_NS=%llu\n", target->name,
        edge_locality_rank_name(rank), (unsigned long long)latency);
    puts("PHASE50_LATENCY_AWARE_SELECTION_OK");
    return release_selected(&f, index) == 0 ? 0 : 1;
}

static int scenario_locality(void)
{
    fixture_t f;
    if (fixture_init(&f) != 0) return 1;
    if (latency_signal_record(&f.latencies[0], 10000000U, 10U) != 0) return 1;
    if (latency_signal_record(&f.latencies[1], 11000000U, 10U) != 0) return 1;
    if (latency_signal_record(&f.latencies[2], 12000000U, 10U) != 0) return 1;
    if (latency_signal_record(&f.latencies[3], 1000000U, 10U) != 0) return 1;

    size_t index = 0;
    edge_locality_rank_t rank;
    uint64_t latency = 0;
    const backend_target_t *target = latency_aware_pool_acquire_next(&f.selector,
        &f.request, 20U, &index, &rank, &latency);
    if (target == NULL) return 1;
    printf("SELECTED=%s RANK=%s EWMA_NS=%llu\n", target->name,
        edge_locality_rank_name(rank), (unsigned long long)latency);
    puts("PHASE50_LATENCY_AWARE_SELECTION_OK");
    return release_selected(&f, index) == 0 ? 0 : 1;
}

static int scenario_no_samples(void)
{
    fixture_t f;
    if (fixture_init(&f) != 0) return 1;
    for (size_t n = 0; n < 3U; ++n) {
        size_t index = 0;
        edge_locality_rank_t rank;
        uint64_t latency = 99U;
        const backend_target_t *target = latency_aware_pool_acquire_next(&f.selector,
            &f.request, 20U, &index, &rank, &latency);
        if (target == NULL) return 1;
        printf("PICK=%s RANK=%s LATENCY_SIGNAL_NS=%llu\n", target->name,
            edge_locality_rank_name(rank), (unsigned long long)latency);
        if (release_selected(&f, index) != 0) return 1;
    }
    puts("PHASE50_LATENCY_AWARE_SELECTION_OK");
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --scenario faster|stale|locality|no-samples\n", program);
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
    if (strcmp(argv[2], "faster") == 0) return scenario_faster();
    if (strcmp(argv[2], "stale") == 0) return scenario_stale();
    if (strcmp(argv[2], "locality") == 0) return scenario_locality();
    if (strcmp(argv[2], "no-samples") == 0) return scenario_no_samples();
    usage(argv[0]);
    return 2;
}
