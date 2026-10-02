#ifndef ROUTING_STABILITY_H
#define ROUTING_STABILITY_H

#include <stddef.h>
#include <stdint.h>

#define ROUTING_STABILITY_MAX_BACKENDS 32U

typedef struct {
    size_t backend_count;
    uint64_t decisions;
    uint64_t no_selection;
    uint64_t switches;
    uint64_t max_consecutive;
    uint64_t current_consecutive;
    size_t last_index;
    int has_last;
    uint64_t picks[ROUTING_STABILITY_MAX_BACKENDS];
} routing_stability_t;

int routing_stability_init(routing_stability_t *stability, size_t backend_count);
int routing_stability_record_pick(routing_stability_t *stability, size_t backend_index);
int routing_stability_record_none(routing_stability_t *stability);
uint64_t routing_stability_max_picks(const routing_stability_t *stability);
uint64_t routing_stability_min_picks(const routing_stability_t *stability);

#endif
