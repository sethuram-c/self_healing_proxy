#include "routing_perf.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    routing_perf_samples_t samples = {0};
    assert(routing_perf_add_sample(&samples, 30) == 0);
    assert(routing_perf_add_sample(&samples, 10) == 0);
    assert(routing_perf_add_sample(&samples, 50) == 0);
    assert(routing_perf_add_sample(&samples, 20) == 0);
    assert(routing_perf_add_sample(&samples, 40) == 0);

    uint64_t median = 0;
    assert(routing_perf_median(&samples, &median) == 0);
    assert(median == 30);

    routing_perf_samples_t even = {0};
    assert(routing_perf_add_sample(&even, 10) == 0);
    assert(routing_perf_add_sample(&even, 20) == 0);
    assert(routing_perf_add_sample(&even, 30) == 0);
    assert(routing_perf_add_sample(&even, 40) == 0);
    assert(routing_perf_median(&even, &median) == 0);
    assert(median == 25);

    assert(routing_perf_ns_per_op_x1000(1000, 10) == 100000);
    assert(routing_perf_ratio_x1000(500000, 100000) == 5000);

    errno = 0;
    assert(routing_perf_add_sample(NULL, 1) == -1);
    assert(errno == EINVAL);
    assert(routing_perf_ns_per_op_x1000(1, 0) == UINT64_MAX);
    assert(routing_perf_ratio_x1000(1, 0) == UINT64_MAX);

    printf("routing performance statistics tests passed\n");
    return 0;
}
