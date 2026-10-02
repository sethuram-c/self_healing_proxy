#include "adaptive_aware_pool.h"
#include "routing_stability.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BACKEND_COUNT 4U
#define SITE_COUNT 3U

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
    for (size_t i = 0; i < SITE_COUNT; ++i) {
        if (edge_locality_set(&f->localities[i], "r1", "z1", "s1") != 0) return -1;
    }
    if (edge_locality_set(&f->localities[3], "r2", "z9", "s9") != 0) return -1;
    return adaptive_aware_pool_init(&f->selector, &f->pool, f->health, f->breakers,
        f->localities, f->latencies, BACKEND_COUNT, &SCORE_CFG);
}

static int select_and_release(fixture_t *f, uint64_t now_ns, routing_stability_t *s,
    edge_locality_rank_t *rank_out, size_t *index_out)
{
    size_t index = 0;
    edge_locality_rank_t rank;
    adaptive_score_result_t score;
    const backend_target_t *target = adaptive_aware_pool_acquire_next(&f->selector,
        &f->request, now_ns, &index, &rank, &score);
    if (target == NULL) {
        return routing_stability_record_none(s);
    }
    if (routing_stability_record_pick(s, index) != 0) return -1;
    if (rank_out != NULL) *rank_out = rank;
    if (index_out != NULL) *index_out = index;
    return backend_pool_release(&f->pool, index);
}

static int scenario_oscillation(void)
{
    fixture_t f;
    routing_stability_t s;
    if (fixture_init(&f) != 0 || routing_stability_init(&s, SITE_COUNT) != 0) return 1;

    for (uint64_t n = 0; n < 3000U; ++n) {
        uint64_t now = 10U + n;
        uint64_t phase = n % 6U;
        uint64_t a = 2000000U + (phase == 0U ? 250000U : 0U);
        uint64_t b = 2050000U + (phase == 2U ? 250000U : 0U);
        uint64_t c = 2100000U + (phase == 4U ? 250000U : 0U);
        if (latency_signal_record(&f.latencies[0], a, now) != 0
            || latency_signal_record(&f.latencies[1], b, now) != 0
            || latency_signal_record(&f.latencies[2], c, now) != 0) return 1;

        f.targets[0].active_connections = (size_t)((n / 7U) % 4U);
        f.targets[1].active_connections = (size_t)((n / 11U) % 4U);
        f.targets[2].active_connections = (size_t)((n / 13U) % 4U);
        edge_locality_rank_t rank;
        if (select_and_release(&f, now, &s, &rank, NULL) != 0 || rank != EDGE_LOCALITY_SITE) return 1;
    }

    uint64_t max = routing_stability_max_picks(&s);
    uint64_t min = routing_stability_min_picks(&s);
    printf("SCENARIO=oscillation DECISIONS=%llu NO_SELECTION=%llu SWITCHES=%llu MAX_STREAK=%llu PICKS_A=%llu PICKS_B=%llu PICKS_C=%llu SPREAD=%llu\n",
        (unsigned long long)s.decisions, (unsigned long long)s.no_selection,
        (unsigned long long)s.switches, (unsigned long long)s.max_consecutive,
        (unsigned long long)s.picks[0], (unsigned long long)s.picks[1],
        (unsigned long long)s.picks[2], (unsigned long long)(max - min));
    if (s.no_selection != 0U || s.max_consecutive > 25U || max - min > 600U) return 1;
    puts("PHASE52_ADAPTIVE_STABILITY_OK");
    return 0;
}

static int scenario_overload(void)
{
    fixture_t f;
    routing_stability_t s;
    if (fixture_init(&f) != 0 || routing_stability_init(&s, SITE_COUNT) != 0) return 1;
    for (size_t i = 0; i < SITE_COUNT; ++i) {
        if (latency_signal_record(&f.latencies[i], 3000000U, 10U) != 0) return 1;
    }

    uint64_t first_half[SITE_COUNT] = {0U, 0U, 0U};
    uint64_t second_half[SITE_COUNT] = {0U, 0U, 0U};
    for (uint64_t n = 0; n < 1500U; ++n) {
        uint64_t now = 20U + n;
        f.targets[0].active_connections = n < 750U ? 9U : 1U;
        f.targets[1].active_connections = 2U;
        f.targets[2].active_connections = n < 750U ? 3U : 9U;
        edge_locality_rank_t rank;
        size_t index = 0;
        if (select_and_release(&f, now, &s, &rank, &index) != 0 || rank != EDGE_LOCALITY_SITE) return 1;
        if (index < SITE_COUNT) {
            if (n < 750U) first_half[index]++; else second_half[index]++;
        }
    }

    printf("SCENARIO=overload DECISIONS=%llu NO_SELECTION=%llu FIRST_A=%llu FIRST_B=%llu FIRST_C=%llu SECOND_A=%llu SECOND_B=%llu SECOND_C=%llu\n",
        (unsigned long long)s.decisions, (unsigned long long)s.no_selection,
        (unsigned long long)first_half[0], (unsigned long long)first_half[1], (unsigned long long)first_half[2],
        (unsigned long long)second_half[0], (unsigned long long)second_half[1], (unsigned long long)second_half[2]);
    if (s.no_selection != 0U || first_half[1] <= first_half[0] || first_half[1] <= first_half[2]
        || second_half[0] <= second_half[2]) return 1;
    puts("PHASE52_ADAPTIVE_STABILITY_OK");
    return 0;
}

static int scenario_stale(void)
{
    fixture_t f;
    routing_stability_t s;
    if (fixture_init(&f) != 0 || routing_stability_init(&s, SITE_COUNT) != 0) return 1;
    if (latency_signal_record(&f.latencies[0], 1000000U, 10U) != 0
        || latency_signal_record(&f.latencies[1], 4000000U, 90U) != 0
        || latency_signal_record(&f.latencies[2], 4200000U, 90U) != 0) return 1;

    for (uint64_t n = 0; n < 600U; ++n) {
        uint64_t now = 100U + n;
        if ((n % 10U) == 0U) {
            if (latency_signal_record(&f.latencies[1], 4000000U, now) != 0
                || latency_signal_record(&f.latencies[2], 4200000U, now) != 0) return 1;
        }
        edge_locality_rank_t rank;
        if (select_and_release(&f, now, &s, &rank, NULL) != 0 || rank != EDGE_LOCALITY_SITE) return 1;
    }
    printf("SCENARIO=stale DECISIONS=%llu PICKS_STALE_FAST=%llu PICKS_FRESH_B=%llu PICKS_FRESH_C=%llu\n",
        (unsigned long long)s.decisions, (unsigned long long)s.picks[0],
        (unsigned long long)s.picks[1], (unsigned long long)s.picks[2]);
    if (s.picks[0] >= s.picks[1] || s.picks[0] >= s.picks[2]) return 1;
    puts("PHASE52_ADAPTIVE_STABILITY_OK");
    return 0;
}

static int scenario_failover(void)
{
    fixture_t f;
    routing_stability_t s;
    if (fixture_init(&f) != 0 || routing_stability_init(&s, BACKEND_COUNT) != 0) return 1;
    for (size_t i = 0; i < BACKEND_COUNT; ++i) {
        if (latency_signal_record(&f.latencies[i], 2000000U + i * 100000U, 10U) != 0) return 1;
    }

    for (uint64_t n = 0; n < 300U; ++n) {
        if (n == 100U) {
            for (size_t i = 0; i < SITE_COUNT; ++i) {
                if (backend_health_record_failure(&f.health[i], 200U) != 0
                    || backend_health_record_failure(&f.health[i], 201U) != 0
                    || backend_health_record_failure(&f.health[i], 202U) != 0) return 1;
            }
        }
        edge_locality_rank_t rank;
        if (select_and_release(&f, 300U + n, &s, &rank, NULL) != 0) return 1;
        if (n < 100U && rank != EDGE_LOCALITY_SITE) return 1;
        if (n >= 100U && rank != EDGE_LOCALITY_REMOTE) return 1;
    }
    printf("SCENARIO=failover DECISIONS=%llu SITE_PICKS=%llu REMOTE_PICKS=%llu\n",
        (unsigned long long)s.decisions,
        (unsigned long long)(s.picks[0] + s.picks[1] + s.picks[2]),
        (unsigned long long)s.picks[3]);
    if (s.picks[3] != 200U) return 1;
    puts("PHASE52_ADAPTIVE_STABILITY_OK");
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --scenario oscillation|overload|stale|failover\n", program);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(argv[0]); return 0; }
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) { usage(argv[0]); return 2; }
    if (strcmp(argv[2], "oscillation") == 0) return scenario_oscillation();
    if (strcmp(argv[2], "overload") == 0) return scenario_overload();
    if (strcmp(argv[2], "stale") == 0) return scenario_stale();
    if (strcmp(argv[2], "failover") == 0) return scenario_failover();
    usage(argv[0]);
    return 2;
}
