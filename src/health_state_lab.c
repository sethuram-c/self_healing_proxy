#include "backend_health.h"

#include <stdio.h>
#include <string.h>

static void print_state(const char *step, const backend_health_t *health)
{
    printf("STEP=%s STATE=%s FAIL_STREAK=%zu SUCCESS_STREAK=%zu ELIGIBLE=%d TRANSITIONS=%llu\n",
           step,
           backend_health_state_name(health->state),
           health->consecutive_failures,
           health->consecutive_successes,
           backend_health_is_eligible(health),
           (unsigned long long)health->transitions);
}

static int run_trip_recover(void)
{
    backend_health_t health;
    const backend_health_config_t config = {3, 2, 100};
    if (backend_health_init(&health, &config) != 0) {
        return 1;
    }

    print_state("init", &health);
    backend_health_record_success(&health, 10);
    print_state("first_success", &health);
    backend_health_record_failure(&health, 20);
    print_state("failure_1", &health);
    backend_health_record_failure(&health, 30);
    print_state("failure_2", &health);
    backend_health_record_failure(&health, 40);
    print_state("failure_3", &health);
    backend_health_tick(&health, 140);
    print_state("cooldown_elapsed", &health);
    backend_health_record_success(&health, 150);
    print_state("recovery_success_1", &health);
    backend_health_record_success(&health, 160);
    print_state("recovery_success_2", &health);
    puts("PHASE39_HEALTH_MODEL_OK");
    return 0;
}

static int run_transient(void)
{
    backend_health_t health;
    const backend_health_config_t config = {3, 2, 100};
    if (backend_health_init(&health, &config) != 0) {
        return 1;
    }

    backend_health_record_success(&health, 10);
    print_state("healthy", &health);
    backend_health_record_failure(&health, 20);
    print_state("single_failure", &health);
    backend_health_record_success(&health, 30);
    print_state("success_after_failure", &health);
    puts("PHASE39_HEALTH_MODEL_OK");
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s --scenario transient|trip-recover\n", argv0);
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
    if (strcmp(argv[2], "transient") == 0) {
        return run_transient();
    }
    if (strcmp(argv[2], "trip-recover") == 0) {
        return run_trip_recover();
    }
    usage(argv[0]);
    return 2;
}
