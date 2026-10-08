#include "routing_policy_manager.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s --scenario reload|rollback|invalid|stale-writer [--config PATH]\n", prog);
}

static void print_policy(const routing_policy_t *p, uint64_t generation)
{
    printf("GENERATION=%llu ALPHA=%u/%u STALE_MS=%llu LATENCY_WEIGHT=%u LOAD_WEIGHT=%u SCORE_SLACK=%u\n",
        (unsigned long long)generation,
        p->latency.alpha_numerator, p->latency.alpha_denominator,
        (unsigned long long)routing_policy_stale_after_ms(p),
        p->score.latency_weight, p->score.load_weight, p->score.score_slack);
}

int main(int argc, char **argv)
{
    const char *scenario = NULL;
    const char *config = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config = argv[++i];
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

    routing_policy_t initial;
    routing_policy_defaults(&initial);
    routing_policy_manager_t manager;
    if (routing_policy_manager_init(&manager, &initial) != 0) return 1;

    const char *tuned =
        "latency_alpha_numerator=3\nlatency_alpha_denominator=8\n"
        "latency_stale_after_ms=9000\nlatency_weight=10\nload_weight=1\nscore_slack=7\n";
    uint64_t generation = 1U;
    uint64_t next = 0;

    if (strcmp(scenario, "reload") == 0) {
        int rc = config != NULL
            ? routing_policy_manager_reload_file(&manager, config, generation, &next)
            : routing_policy_manager_reload_text(&manager, tuned, generation, &next);
        if (rc != 0) return 1;
    } else if (strcmp(scenario, "rollback") == 0) {
        if (routing_policy_manager_reload_text(&manager, tuned, generation, &next) != 0) return 1;
        generation = next;
        if (routing_policy_manager_rollback(&manager, generation, &next) != 0) return 1;
    } else if (strcmp(scenario, "invalid") == 0) {
        int rc = routing_policy_manager_reload_text(&manager,
            "latency_weight=0\nload_weight=0\n", generation, &next);
        if (rc != EINVAL) return 1;
        routing_policy_t snap;
        if (routing_policy_manager_snapshot(&manager, &snap, &generation) != 0) return 1;
        print_policy(&snap, generation);
        puts("INVALID_RELOAD_REJECTED=1");
        puts("PHASE58_POLICY_RELOAD_OK");
        return 0;
    } else if (strcmp(scenario, "stale-writer") == 0) {
        if (routing_policy_manager_reload_text(&manager, tuned, generation, &next) != 0) return 1;
        int rc = routing_policy_manager_reload_text(&manager,
            "latency_weight=1\nload_weight=9\n", generation, &next);
        if (rc != EAGAIN) return 1;
        routing_policy_t snap;
        if (routing_policy_manager_snapshot(&manager, &snap, &generation) != 0) return 1;
        print_policy(&snap, generation);
        puts("STALE_WRITER_REJECTED=1");
        puts("PHASE58_POLICY_RELOAD_OK");
        return 0;
    } else {
        usage(argv[0]);
        return 2;
    }

    routing_policy_t snap;
    if (routing_policy_manager_snapshot(&manager, &snap, &generation) != 0) return 1;
    print_policy(&snap, generation);
    puts("PHASE58_POLICY_RELOAD_OK");
    return 0;
}
