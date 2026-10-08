#include "routing_policy_manager.h"

#include <errno.h>
#include <sched.h>
#include <stddef.h>

static void writer_lock(routing_policy_manager_t *manager)
{
    while (atomic_flag_test_and_set_explicit(&manager->writer_lock, memory_order_acquire)) {
        sched_yield();
    }
}

static void writer_unlock(routing_policy_manager_t *manager)
{
    atomic_flag_clear_explicit(&manager->writer_lock, memory_order_release);
}

static void publish_locked(routing_policy_manager_t *manager,
    const routing_policy_t *policy, uint64_t generation)
{
    uint64_t seq = atomic_load_explicit(&manager->sequence, memory_order_relaxed);
    if ((seq & 1U) != 0U) {
        ++seq;
    }
    atomic_store_explicit(&manager->sequence, seq + 1U, memory_order_release);
    atomic_store_explicit(&manager->alpha_numerator,
        policy->latency.alpha_numerator, memory_order_relaxed);
    atomic_store_explicit(&manager->alpha_denominator,
        policy->latency.alpha_denominator, memory_order_relaxed);
    atomic_store_explicit(&manager->stale_after_ns,
        policy->latency.stale_after_ns, memory_order_relaxed);
    atomic_store_explicit(&manager->latency_weight,
        policy->score.latency_weight, memory_order_relaxed);
    atomic_store_explicit(&manager->load_weight,
        policy->score.load_weight, memory_order_relaxed);
    atomic_store_explicit(&manager->score_slack,
        policy->score.score_slack, memory_order_relaxed);
    atomic_store_explicit(&manager->generation, generation, memory_order_relaxed);
    atomic_store_explicit(&manager->sequence, seq + 2U, memory_order_release);
}

int routing_policy_manager_init(routing_policy_manager_t *manager,
    const routing_policy_t *initial)
{
    if (manager == NULL || routing_policy_validate(initial) != 0) {
        return EINVAL;
    }
    atomic_init(&manager->sequence, 0U);
    atomic_init(&manager->generation, 1U);
    atomic_init(&manager->alpha_numerator, initial->latency.alpha_numerator);
    atomic_init(&manager->alpha_denominator, initial->latency.alpha_denominator);
    atomic_init(&manager->stale_after_ns, initial->latency.stale_after_ns);
    atomic_init(&manager->latency_weight, initial->score.latency_weight);
    atomic_init(&manager->load_weight, initial->score.load_weight);
    atomic_init(&manager->score_slack, initial->score.score_slack);
    atomic_flag_clear(&manager->writer_lock);
    manager->has_previous = 0;
    manager->previous = *initial;
    return 0;
}

int routing_policy_manager_snapshot(const routing_policy_manager_t *manager,
    routing_policy_t *policy, uint64_t *generation)
{
    if (manager == NULL || policy == NULL || generation == NULL) {
        return EINVAL;
    }

    for (;;) {
        uint64_t before = atomic_load_explicit(&manager->sequence, memory_order_acquire);
        if ((before & 1U) != 0U) {
            sched_yield();
            continue;
        }

        routing_policy_t candidate;
        candidate.latency.alpha_numerator = atomic_load_explicit(
            &manager->alpha_numerator, memory_order_relaxed);
        candidate.latency.alpha_denominator = atomic_load_explicit(
            &manager->alpha_denominator, memory_order_relaxed);
        candidate.latency.stale_after_ns = atomic_load_explicit(
            &manager->stale_after_ns, memory_order_relaxed);
        candidate.score.latency_weight = atomic_load_explicit(
            &manager->latency_weight, memory_order_relaxed);
        candidate.score.load_weight = atomic_load_explicit(
            &manager->load_weight, memory_order_relaxed);
        candidate.score.score_slack = atomic_load_explicit(
            &manager->score_slack, memory_order_relaxed);
        uint64_t gen = atomic_load_explicit(&manager->generation, memory_order_relaxed);

        uint64_t after = atomic_load_explicit(&manager->sequence, memory_order_acquire);
        if (before == after && (after & 1U) == 0U) {
            if (routing_policy_validate(&candidate) != 0) {
                return EPROTO;
            }
            *policy = candidate;
            *generation = gen;
            return 0;
        }
    }
}

static void snapshot_locked(const routing_policy_manager_t *manager,
    routing_policy_t *policy, uint64_t *generation)
{
    policy->latency.alpha_numerator = atomic_load_explicit(
        &manager->alpha_numerator, memory_order_relaxed);
    policy->latency.alpha_denominator = atomic_load_explicit(
        &manager->alpha_denominator, memory_order_relaxed);
    policy->latency.stale_after_ns = atomic_load_explicit(
        &manager->stale_after_ns, memory_order_relaxed);
    policy->score.latency_weight = atomic_load_explicit(
        &manager->latency_weight, memory_order_relaxed);
    policy->score.load_weight = atomic_load_explicit(
        &manager->load_weight, memory_order_relaxed);
    policy->score.score_slack = atomic_load_explicit(
        &manager->score_slack, memory_order_relaxed);
    *generation = atomic_load_explicit(&manager->generation, memory_order_relaxed);
}

int routing_policy_manager_apply(routing_policy_manager_t *manager,
    const routing_policy_t *candidate, uint64_t expected_generation,
    uint64_t *new_generation)
{
    if (manager == NULL || new_generation == NULL
        || routing_policy_validate(candidate) != 0) {
        return EINVAL;
    }

    writer_lock(manager);
    routing_policy_t current;
    uint64_t current_generation = 0;
    snapshot_locked(manager, &current, &current_generation);
    if (current_generation != expected_generation) {
        writer_unlock(manager);
        return EAGAIN;
    }
    if (current_generation == UINT64_MAX) {
        writer_unlock(manager);
        return EOVERFLOW;
    }

    manager->previous = current;
    manager->has_previous = 1;
    uint64_t next = current_generation + 1U;
    publish_locked(manager, candidate, next);
    writer_unlock(manager);
    *new_generation = next;
    return 0;
}

int routing_policy_manager_reload_text(routing_policy_manager_t *manager,
    const char *text, uint64_t expected_generation, uint64_t *new_generation)
{
    routing_policy_t candidate;
    if (text == NULL || routing_policy_parse(text, &candidate) != 0) {
        return EINVAL;
    }
    return routing_policy_manager_apply(manager, &candidate,
        expected_generation, new_generation);
}

int routing_policy_manager_reload_file(routing_policy_manager_t *manager,
    const char *path, uint64_t expected_generation, uint64_t *new_generation)
{
    routing_policy_t candidate;
    if (path == NULL || routing_policy_load_file(path, &candidate) != 0) {
        return EINVAL;
    }
    return routing_policy_manager_apply(manager, &candidate,
        expected_generation, new_generation);
}

int routing_policy_manager_rollback(routing_policy_manager_t *manager,
    uint64_t expected_generation, uint64_t *new_generation)
{
    if (manager == NULL || new_generation == NULL) {
        return EINVAL;
    }

    writer_lock(manager);
    routing_policy_t current;
    uint64_t current_generation = 0;
    snapshot_locked(manager, &current, &current_generation);
    if (current_generation != expected_generation) {
        writer_unlock(manager);
        return EAGAIN;
    }
    if (!manager->has_previous) {
        writer_unlock(manager);
        return ENOENT;
    }
    if (current_generation == UINT64_MAX) {
        writer_unlock(manager);
        return EOVERFLOW;
    }

    routing_policy_t rollback = manager->previous;
    manager->previous = current;
    uint64_t next = current_generation + 1U;
    publish_locked(manager, &rollback, next);
    writer_unlock(manager);
    *new_generation = next;
    return 0;
}
