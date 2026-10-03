#include "routing_perf.h"

#include <errno.h>
#include <stdint.h>

int routing_perf_add_sample(routing_perf_samples_t *samples, uint64_t elapsed_ns)
{
    if (samples == NULL || elapsed_ns == 0
        || samples->count >= ROUTING_PERF_MAX_SAMPLES) {
        errno = EINVAL;
        return -1;
    }
    samples->samples_ns[samples->count++] = elapsed_ns;
    return 0;
}

int routing_perf_median(const routing_perf_samples_t *samples, uint64_t *median_ns)
{
    if (samples == NULL || median_ns == NULL || samples->count == 0
        || samples->count > ROUTING_PERF_MAX_SAMPLES) {
        errno = EINVAL;
        return -1;
    }

    uint64_t tmp[ROUTING_PERF_MAX_SAMPLES];
    for (size_t i = 0; i < samples->count; ++i) {
        tmp[i] = samples->samples_ns[i];
    }
    for (size_t i = 1; i < samples->count; ++i) {
        uint64_t value = tmp[i];
        size_t j = i;
        while (j > 0 && tmp[j - 1U] > value) {
            tmp[j] = tmp[j - 1U];
            --j;
        }
        tmp[j] = value;
    }

    size_t mid = samples->count / 2U;
    if ((samples->count & 1U) != 0U) {
        *median_ns = tmp[mid];
    } else {
        uint64_t a = tmp[mid - 1U];
        uint64_t b = tmp[mid];
        *median_ns = a + (b - a) / 2U;
    }
    return 0;
}

uint64_t routing_perf_ns_per_op_x1000(uint64_t elapsed_ns, uint64_t operations)
{
    if (operations == 0) {
        return UINT64_MAX;
    }
    if (elapsed_ns > UINT64_MAX / 1000U) {
        return UINT64_MAX;
    }
    return (elapsed_ns * 1000U) / operations;
}

uint64_t routing_perf_ratio_x1000(uint64_t numerator_ns_per_op_x1000,
    uint64_t denominator_ns_per_op_x1000)
{
    if (denominator_ns_per_op_x1000 == 0) {
        return UINT64_MAX;
    }
    if (numerator_ns_per_op_x1000 > UINT64_MAX / 1000U) {
        return UINT64_MAX;
    }
    return (numerator_ns_per_op_x1000 * 1000U) / denominator_ns_per_op_x1000;
}
