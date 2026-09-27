#include "healing_coordinator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static healing_coordinator_t init_coord(size_t max_concurrent, size_t max_attempts, uint64_t window) {
    healing_coordinator_t c;
    healing_coordinator_config_t cfg = {4, max_concurrent, max_attempts, window};
    if (healing_coordinator_init(&c, &cfg) != 0) {
        perror("healing_coordinator_init");
        exit(1);
    }
    return c;
}

static void request_or_die(healing_coordinator_t *c, size_t idx, uint64_t now) {
    if (healing_coordinator_request(c, idx, now) != 0) { perror("request"); exit(1); }
}

static int scenario_concurrency(void) {
    healing_coordinator_t c = init_coord(2, 10, 1000);
    size_t idx = 0; healing_admit_reason_t r;
    for (size_t i = 0; i < 4; i++) request_or_die(&c, i, 10);
    for (int i = 0; i < 2; i++) {
        if (healing_coordinator_acquire_next(&c, 10, &idx, &r) != 1) return 1;
        printf("ADMIT backend=%zu reason=%s active=%zu\n", idx, healing_admit_reason_name(r), c.active_count);
    }
    if (healing_coordinator_acquire_next(&c, 10, &idx, &r) != 0) return 1;
    printf("DENY reason=%s active=%zu pending=%zu\n", healing_admit_reason_name(r), c.active_count, healing_coordinator_pending_count(&c));
    if (r != HEALING_ADMIT_CONCURRENCY_LIMIT) return 1;
    if (healing_coordinator_complete(&c, 0, 11) != 0) return 1;
    if (healing_coordinator_acquire_next(&c, 11, &idx, &r) != 1 || idx != 2) return 1;
    printf("AFTER_COMPLETE backend=%zu active=%zu\n", idx, c.active_count);
    puts("PHASE47_FLEET_COORDINATION_OK");
    return 0;
}

static int scenario_fairness(void) {
    healing_coordinator_t c = init_coord(1, 10, 1000);
    size_t idx; healing_admit_reason_t r;
    request_or_die(&c, 0, 1); request_or_die(&c, 1, 1); request_or_die(&c, 2, 1);
    size_t expected[] = {0,1,2,0};
    for (size_t step = 0; step < 4; step++) {
        if (healing_coordinator_acquire_next(&c, step + 1, &idx, &r) != 1 || idx != expected[step]) return 1;
        printf("STEP=%zu ADMIT backend=%zu\n", step + 1, idx);
        if (healing_coordinator_complete(&c, idx, step + 2) != 0) return 1;
        if (step == 0) request_or_die(&c, 0, 2);
    }
    puts("PHASE47_FLEET_COORDINATION_OK");
    return 0;
}

static int scenario_budget(void) {
    healing_coordinator_t c = init_coord(1, 3, 100);
    size_t idx; healing_admit_reason_t r;
    for (size_t i = 0; i < 3; i++) {
        uint64_t t = 10 + i * 10;
        request_or_die(&c, i, t);
        if (healing_coordinator_acquire_next(&c, t, &idx, &r) != 1) return 1;
        if (healing_coordinator_complete(&c, idx, t + 1) != 0) return 1;
    }
    request_or_die(&c, 3, 40);
    if (healing_coordinator_acquire_next(&c, 40, &idx, &r) != 0 || r != HEALING_ADMIT_WINDOW_BUDGET) return 1;
    printf("AT_40 reason=%s attempts=%zu\n", healing_admit_reason_name(r), c.attempt_count);
    if (healing_coordinator_acquire_next(&c, 111, &idx, &r) != 1 || idx != 3) return 1;
    printf("AT_111 backend=%zu reason=%s attempts=%zu\n", idx, healing_admit_reason_name(r), c.attempt_count);
    puts("PHASE47_FLEET_COORDINATION_OK");
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s --scenario concurrency|fairness|budget\n", prog);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(argv[0]); return 0; }
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) { usage(argv[0]); return 2; }
    if (strcmp(argv[2], "concurrency") == 0) return scenario_concurrency();
    if (strcmp(argv[2], "fairness") == 0) return scenario_fairness();
    if (strcmp(argv[2], "budget") == 0) return scenario_budget();
    usage(argv[0]); return 2;
}
