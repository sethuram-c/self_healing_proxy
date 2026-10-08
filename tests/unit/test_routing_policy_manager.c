#include "routing_policy_manager.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

static const char *PROFILE_A =
    "latency_alpha_numerator=1\n"
    "latency_alpha_denominator=4\n"
    "latency_stale_after_ms=5000\n"
    "latency_weight=1\n"
    "load_weight=2\n"
    "score_slack=80\n";

static const char *PROFILE_B =
    "latency_alpha_numerator=3\n"
    "latency_alpha_denominator=8\n"
    "latency_stale_after_ms=9000\n"
    "latency_weight=10\n"
    "load_weight=1\n"
    "score_slack=7\n";

static int is_a(const routing_policy_t *p)
{
    return p->latency.alpha_numerator == 1U
        && p->latency.alpha_denominator == 4U
        && routing_policy_stale_after_ms(p) == 5000U
        && p->score.latency_weight == 1U
        && p->score.load_weight == 2U
        && p->score.score_slack == 80U;
}

static int is_b(const routing_policy_t *p)
{
    return p->latency.alpha_numerator == 3U
        && p->latency.alpha_denominator == 8U
        && routing_policy_stale_after_ms(p) == 9000U
        && p->score.latency_weight == 10U
        && p->score.load_weight == 1U
        && p->score.score_slack == 7U;
}

typedef struct {
    routing_policy_manager_t *manager;
    atomic_int *failed;
    atomic_int *done;
} reader_ctx_t;

static void *reader_main(void *arg)
{
    reader_ctx_t *ctx = arg;
    while (atomic_load_explicit(ctx->done, memory_order_acquire) == 0) {
        routing_policy_t policy;
        uint64_t generation = 0;
        if (routing_policy_manager_snapshot(ctx->manager, &policy, &generation) != 0
            || generation == 0U || (!is_a(&policy) && !is_b(&policy))) {
            atomic_store_explicit(ctx->failed, 1, memory_order_release);
            break;
        }
    }
    return NULL;
}

static void test_init_reload_rollback_and_stale_generation(void)
{
    routing_policy_t initial;
    assert(routing_policy_parse(PROFILE_A, &initial) == 0);
    routing_policy_manager_t manager;
    assert(routing_policy_manager_init(&manager, &initial) == 0);

    routing_policy_t snap;
    uint64_t generation = 0;
    assert(routing_policy_manager_snapshot(&manager, &snap, &generation) == 0);
    assert(generation == 1U && is_a(&snap));

    uint64_t new_generation = 0;
    assert(routing_policy_manager_reload_text(&manager, PROFILE_B, 1U,
        &new_generation) == 0);
    assert(new_generation == 2U);
    assert(routing_policy_manager_snapshot(&manager, &snap, &generation) == 0);
    assert(generation == 2U && is_b(&snap));

    assert(routing_policy_manager_reload_text(&manager, PROFILE_A, 1U,
        &new_generation) == EAGAIN);
    assert(routing_policy_manager_snapshot(&manager, &snap, &generation) == 0);
    assert(generation == 2U && is_b(&snap));

    assert(routing_policy_manager_rollback(&manager, 2U, &new_generation) == 0);
    assert(new_generation == 3U);
    assert(routing_policy_manager_snapshot(&manager, &snap, &generation) == 0);
    assert(generation == 3U && is_a(&snap));
}

static void test_invalid_reload_does_not_publish(void)
{
    routing_policy_t initial;
    assert(routing_policy_parse(PROFILE_A, &initial) == 0);
    routing_policy_manager_t manager;
    assert(routing_policy_manager_init(&manager, &initial) == 0);
    uint64_t next = 0;
    assert(routing_policy_manager_reload_text(&manager,
        "latency_weight=0\nload_weight=0\n", 1U, &next) == EINVAL);
    routing_policy_t snap;
    uint64_t generation = 0;
    assert(routing_policy_manager_snapshot(&manager, &snap, &generation) == 0);
    assert(generation == 1U && is_a(&snap));
    assert(routing_policy_manager_rollback(&manager, 1U, &next) == ENOENT);
}

static void test_concurrent_readers_never_observe_hybrid(void)
{
    routing_policy_t initial;
    assert(routing_policy_parse(PROFILE_A, &initial) == 0);
    routing_policy_manager_t manager;
    assert(routing_policy_manager_init(&manager, &initial) == 0);

    atomic_int failed;
    atomic_int done;
    atomic_init(&failed, 0);
    atomic_init(&done, 0);
    reader_ctx_t ctx = {&manager, &failed, &done};
    pthread_t readers[4];
    for (size_t i = 0; i < 4U; ++i) {
        assert(pthread_create(&readers[i], NULL, reader_main, &ctx) == 0);
    }

    uint64_t generation = 1U;
    for (size_t i = 0; i < 20000U; ++i) {
        uint64_t next = 0;
        const char *profile = (i & 1U) == 0U ? PROFILE_B : PROFILE_A;
        int rc = routing_policy_manager_reload_text(&manager, profile,
            generation, &next);
        assert(rc == 0);
        assert(next == generation + 1U);
        generation = next;
    }

    atomic_store_explicit(&done, 1, memory_order_release);
    for (size_t i = 0; i < 4U; ++i) {
        assert(pthread_join(readers[i], NULL) == 0);
    }
    assert(atomic_load_explicit(&failed, memory_order_acquire) == 0);
}

int main(void)
{
    test_init_reload_rollback_and_stale_generation();
    test_invalid_reload_does_not_publish();
    test_concurrent_readers_never_observe_hybrid();
    puts("routing policy reload manager tests passed");
    return 0;
}
