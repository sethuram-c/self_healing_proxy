#include "circuit_breaker.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

static circuit_breaker_t make_breaker(void)
{
    circuit_breaker_t b;
    circuit_breaker_config_t c = {3, 100, 50};
    assert(circuit_breaker_init(&b, &c) == 0);
    return b;
}

int main(void)
{
    circuit_breaker_t b = make_breaker();
    assert(b.state == CIRCUIT_BREAKER_CLOSED);
    assert(circuit_breaker_record_failure(&b, 10) == 0);
    assert(circuit_breaker_record_failure(&b, 20) == 0);
    assert(b.state == CIRCUIT_BREAKER_CLOSED);
    assert(circuit_breaker_record_failure(&b, 30) == 0);
    assert(b.state == CIRCUIT_BREAKER_OPEN);
    assert(circuit_breaker_try_acquire(&b, 79) == 0);
    assert(errno == EAGAIN);
    assert(circuit_breaker_try_acquire(&b, 80) == 1);
    assert(b.state == CIRCUIT_BREAKER_HALF_OPEN);
    assert(circuit_breaker_try_acquire(&b, 80) == 0);
    assert(circuit_breaker_record_success(&b, 81) == 0);
    assert(b.state == CIRCUIT_BREAKER_CLOSED);

    b = make_breaker();
    assert(circuit_breaker_record_failure(&b, 10) == 0);
    assert(circuit_breaker_record_failure(&b, 120) == 0);
    assert(circuit_breaker_record_failure(&b, 130) == 0);
    assert(b.state == CIRCUIT_BREAKER_CLOSED);
    assert(circuit_breaker_recent_failures(&b, 130) == 2);

    b = make_breaker();
    assert(circuit_breaker_record_failure(&b, 1) == 0);
    assert(circuit_breaker_record_failure(&b, 2) == 0);
    assert(circuit_breaker_record_failure(&b, 3) == 0);
    assert(circuit_breaker_try_acquire(&b, 53) == 1);
    assert(circuit_breaker_record_failure(&b, 54) == 0);
    assert(b.state == CIRCUIT_BREAKER_OPEN);
    assert(b.opens == 2);

    errno = 0;
    assert(circuit_breaker_record_failure(&b, 53) == -1);
    assert(errno == EINVAL);

    puts("circuit breaker tests passed");
    return 0;
}
