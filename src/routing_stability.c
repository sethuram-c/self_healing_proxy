#include "routing_stability.h"

#include <string.h>

int routing_stability_init(routing_stability_t *stability, size_t backend_count)
{
    if (stability == NULL || backend_count == 0 || backend_count > ROUTING_STABILITY_MAX_BACKENDS) {
        return -1;
    }
    memset(stability, 0, sizeof(*stability));
    stability->backend_count = backend_count;
    return 0;
}

int routing_stability_record_pick(routing_stability_t *stability, size_t backend_index)
{
    if (stability == NULL || backend_index >= stability->backend_count) {
        return -1;
    }
    stability->decisions++;
    stability->picks[backend_index]++;
    if (stability->has_last && stability->last_index == backend_index) {
        stability->current_consecutive++;
    } else {
        if (stability->has_last) {
            stability->switches++;
        }
        stability->last_index = backend_index;
        stability->has_last = 1;
        stability->current_consecutive = 1;
    }
    if (stability->current_consecutive > stability->max_consecutive) {
        stability->max_consecutive = stability->current_consecutive;
    }
    return 0;
}

int routing_stability_record_none(routing_stability_t *stability)
{
    if (stability == NULL) return -1;
    stability->decisions++;
    stability->no_selection++;
    stability->has_last = 0;
    stability->current_consecutive = 0;
    return 0;
}

uint64_t routing_stability_max_picks(const routing_stability_t *stability)
{
    if (stability == NULL || stability->backend_count == 0) return 0;
    uint64_t max = stability->picks[0];
    for (size_t i = 1; i < stability->backend_count; ++i) {
        if (stability->picks[i] > max) max = stability->picks[i];
    }
    return max;
}

uint64_t routing_stability_min_picks(const routing_stability_t *stability)
{
    if (stability == NULL || stability->backend_count == 0) return 0;
    uint64_t min = stability->picks[0];
    for (size_t i = 1; i < stability->backend_count; ++i) {
        if (stability->picks[i] < min) min = stability->picks[i];
    }
    return min;
}
