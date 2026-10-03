#ifndef ROUTING_PERF_H
#define ROUTING_PERF_H

#include <stddef.h>
#include <stdint.h>

#define ROUTING_PERF_MAX_SAMPLES 15U

typedef struct {
    uint64_t samples_ns[ROUTING_PERF_MAX_SAMPLES];
    size_t count;
} routing_perf_samples_t;

int routing_perf_add_sample(routing_perf_samples_t *samples, uint64_t elapsed_ns);
int routing_perf_median(const routing_perf_samples_t *samples, uint64_t *median_ns);
uint64_t routing_perf_ns_per_op_x1000(uint64_t elapsed_ns, uint64_t operations);
uint64_t routing_perf_ratio_x1000(uint64_t numerator_ns_per_op_x1000,
    uint64_t denominator_ns_per_op_x1000);

#endif
