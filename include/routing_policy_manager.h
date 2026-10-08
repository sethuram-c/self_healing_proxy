#ifndef ROUTING_POLICY_MANAGER_H
#define ROUTING_POLICY_MANAGER_H

#include <stdatomic.h>
#include <stdint.h>

#include "routing_policy.h"

typedef struct {
    atomic_uint_fast64_t sequence;
    atomic_uint_fast64_t generation;
    atomic_uint alpha_numerator;
    atomic_uint alpha_denominator;
    atomic_uint_fast64_t stale_after_ns;
    atomic_uint latency_weight;
    atomic_uint load_weight;
    atomic_uint score_slack;
    atomic_flag writer_lock;
    routing_policy_t previous;
    int has_previous;
} routing_policy_manager_t;

int routing_policy_manager_init(routing_policy_manager_t *manager,
    const routing_policy_t *initial);
int routing_policy_manager_snapshot(const routing_policy_manager_t *manager,
    routing_policy_t *policy, uint64_t *generation);
int routing_policy_manager_apply(routing_policy_manager_t *manager,
    const routing_policy_t *candidate, uint64_t expected_generation,
    uint64_t *new_generation);
int routing_policy_manager_reload_text(routing_policy_manager_t *manager,
    const char *text, uint64_t expected_generation, uint64_t *new_generation);
int routing_policy_manager_reload_file(routing_policy_manager_t *manager,
    const char *path, uint64_t expected_generation, uint64_t *new_generation);
int routing_policy_manager_rollback(routing_policy_manager_t *manager,
    uint64_t expected_generation, uint64_t *new_generation);

#endif
