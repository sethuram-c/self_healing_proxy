#include "backend_health.h"
#include "backend_pool.h"
#include "health_aware_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const backend_health_config_t CFG = {
    .failure_threshold = 2,
    .recovery_success_threshold = 2,
    .cooldown_ns = 100,
};

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s --scenario skip|all-unavailable|rejoin\n", prog);
}

static int init_fixture(
    backend_target_t targets[3],
    backend_pool_t *pool,
    backend_health_t health[3],
    health_aware_pool_t *selector
)
{
    targets[0] = (backend_target_t){"backend-a", "127.0.0.1", 9101, 0, 8};
    targets[1] = (backend_target_t){"backend-b", "127.0.0.1", 9102, 0, 8};
    targets[2] = (backend_target_t){"backend-c", "127.0.0.1", 9103, 0, 8};

    if (backend_pool_init(pool, targets, 3) != 0) {
        return -1;
    }

    for (size_t i = 0; i < 3; ++i) {
        if (backend_health_init(&health[i], &CFG) != 0) {
            return -1;
        }
        if (backend_health_record_success(&health[i], 1) != 0) {
            return -1;
        }
    }

    return health_aware_pool_init(selector, pool, health, 3);
}

static int acquire_print(health_aware_pool_t *selector, const char *step)
{
    size_t index = 0;
    const backend_target_t *target = health_aware_pool_acquire_next(selector, &index);

    if (target == NULL) {
        printf("STEP=%s SELECTED=NONE ELIGIBLE=%zu\n",
               step,
               health_aware_pool_count_eligible(selector));
        return 0;
    }

    printf("STEP=%s SELECTED=%s INDEX=%zu STATE=%s ELIGIBLE=%zu\n",
           step,
           target->name,
           index,
           backend_health_state_name(selector->health[index].state),
           health_aware_pool_count_eligible(selector));

    if (backend_pool_release(selector->pool, index) != 0) {
        return -1;
    }
    return 0;
}

static int scenario_skip(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;

    if (init_fixture(targets, &pool, health, &selector) != 0) {
        return 1;
    }

    if (backend_health_record_failure(&health[1], 2) != 0
        || backend_health_record_failure(&health[1], 3) != 0) {
        return 1;
    }

    if (acquire_print(&selector, "pick_1") != 0
        || acquire_print(&selector, "pick_2") != 0
        || acquire_print(&selector, "pick_3") != 0
        || acquire_print(&selector, "pick_4") != 0) {
        return 1;
    }

    puts("PHASE42_HEALTH_AWARE_SELECTION_OK");
    return 0;
}

static int scenario_all_unavailable(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;

    if (init_fixture(targets, &pool, health, &selector) != 0) {
        return 1;
    }

    for (size_t i = 0; i < 3; ++i) {
        if (backend_health_record_failure(&health[i], 2) != 0
            || backend_health_record_failure(&health[i], 3) != 0) {
            return 1;
        }
    }

    if (acquire_print(&selector, "none") != 0) {
        return 1;
    }

    printf("CURSOR=%zu\n", pool.next_index);
    puts("PHASE42_HEALTH_AWARE_SELECTION_OK");
    return 0;
}

static int scenario_rejoin(void)
{
    backend_target_t targets[3];
    backend_pool_t pool;
    backend_health_t health[3];
    health_aware_pool_t selector;

    if (init_fixture(targets, &pool, health, &selector) != 0) {
        return 1;
    }

    if (backend_health_record_failure(&health[1], 2) != 0
        || backend_health_record_failure(&health[1], 3) != 0) {
        return 1;
    }

    if (acquire_print(&selector, "before_1") != 0
        || acquire_print(&selector, "before_2") != 0) {
        return 1;
    }

    if (backend_health_tick(&health[1], 103) != 0
        || backend_health_record_success(&health[1], 104) != 0
        || backend_health_record_success(&health[1], 105) != 0) {
        return 1;
    }

    printf("REJOIN_STATE=%s ELIGIBLE=%d\n",
           backend_health_state_name(health[1].state),
           backend_health_is_eligible(&health[1]));

    if (acquire_print(&selector, "after_1") != 0
        || acquire_print(&selector, "after_2") != 0
        || acquire_print(&selector, "after_3") != 0) {
        return 1;
    }

    puts("PHASE42_HEALTH_AWARE_SELECTION_OK");
    return 0;
}

int main(int argc, char **argv)
{
    const char *scenario = NULL;

    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(argv[0]);
        return 0;
    }

    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) {
        usage(argv[0]);
        return 2;
    }

    scenario = argv[2];
    if (strcmp(scenario, "skip") == 0) {
        return scenario_skip();
    }
    if (strcmp(scenario, "all-unavailable") == 0) {
        return scenario_all_unavailable();
    }
    if (strcmp(scenario, "rejoin") == 0) {
        return scenario_rejoin();
    }

    usage(argv[0]);
    return 2;
}
