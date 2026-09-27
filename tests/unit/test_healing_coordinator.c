#include "healing_coordinator.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

static healing_coordinator_t make_coord(size_t max_concurrent, size_t max_attempts, uint64_t window) {
    healing_coordinator_t c;
    healing_coordinator_config_t cfg = {4, max_concurrent, max_attempts, window};
    assert(healing_coordinator_init(&c, &cfg) == 0);
    return c;
}

int main(void) {
    healing_coordinator_t c = make_coord(2, 3, 100);
    size_t idx = 99;
    healing_admit_reason_t reason = HEALING_ADMIT_NONE;

    assert(healing_coordinator_request(&c, 0, 10) == 0);
    errno = 0;
    assert(healing_coordinator_request(&c, 0, 10) == -1 && errno == EALREADY);
    assert(healing_coordinator_request(&c, 1, 10) == 0);
    assert(healing_coordinator_request(&c, 2, 10) == 0);

    assert(healing_coordinator_acquire_next(&c, 10, &idx, &reason) == 1 && idx == 0);
    assert(healing_coordinator_acquire_next(&c, 10, &idx, &reason) == 1 && idx == 1);
    assert(healing_coordinator_acquire_next(&c, 10, &idx, &reason) == 0 && reason == HEALING_ADMIT_CONCURRENCY_LIMIT);
    assert(healing_coordinator_complete(&c, 0, 11) == 0);
    assert(healing_coordinator_acquire_next(&c, 11, &idx, &reason) == 1 && idx == 2);

    assert(healing_coordinator_complete(&c, 1, 12) == 0);
    assert(healing_coordinator_complete(&c, 2, 12) == 0);
    assert(healing_coordinator_request(&c, 3, 20) == 0);
    assert(healing_coordinator_acquire_next(&c, 20, &idx, &reason) == 0 && reason == HEALING_ADMIT_WINDOW_BUDGET);
    assert(healing_coordinator_acquire_next(&c, 111, &idx, &reason) == 1 && idx == 3);

    healing_coordinator_t fair = make_coord(1, 10, 1000);
    assert(healing_coordinator_request(&fair, 0, 1) == 0);
    assert(healing_coordinator_request(&fair, 1, 1) == 0);
    assert(healing_coordinator_request(&fair, 2, 1) == 0);
    assert(healing_coordinator_acquire_next(&fair, 1, &idx, &reason) == 1 && idx == 0);
    assert(healing_coordinator_complete(&fair, 0, 2) == 0);
    assert(healing_coordinator_request(&fair, 0, 2) == 0);
    assert(healing_coordinator_acquire_next(&fair, 2, &idx, &reason) == 1 && idx == 1);
    assert(healing_coordinator_complete(&fair, 1, 3) == 0);
    assert(healing_coordinator_acquire_next(&fair, 3, &idx, &reason) == 1 && idx == 2);
    assert(healing_coordinator_complete(&fair, 2, 4) == 0);
    assert(healing_coordinator_acquire_next(&fair, 4, &idx, &reason) == 1 && idx == 0);

    errno = 0;
    assert(healing_coordinator_complete(&fair, 3, 5) == -1 && errno == EINVAL);
    errno = 0;
    assert(healing_coordinator_request(&fair, 3, 3) == -1 && errno == ERANGE);

    puts("fleet healing coordinator tests passed");
    return 0;
}
