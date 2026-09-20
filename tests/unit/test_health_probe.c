#include "health_probe.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

static health_probe_t make_probe(void)
{
    health_probe_t probe;
    health_probe_config_t config = {
        .interval_ns = 100,
        .timeout_ns = 20,
    };

    assert(health_probe_init(&probe, "127.0.0.1", 8080, &config, 10) == 0);
    return probe;
}

static void test_configuration_validation(void)
{
    health_probe_t probe;
    health_probe_config_t good = {100, 20};
    health_probe_config_t zero_interval = {0, 20};
    health_probe_config_t zero_timeout = {100, 0};

    assert(health_probe_init(NULL, "127.0.0.1", 80, &good, 0) == -1);
    assert(health_probe_init(&probe, NULL, 80, &good, 0) == -1);
    assert(health_probe_init(&probe, "bad-ip", 80, &good, 0) == -1);
    assert(health_probe_init(&probe, "127.0.0.1", 0, &good, 0) == -1);
    assert(health_probe_init(&probe, "127.0.0.1", 80, &zero_interval, 0) == -1);
    assert(health_probe_init(&probe, "127.0.0.1", 80, &zero_timeout, 0) == -1);
}

static void test_due_schedule_requires_result_consumption(void)
{
    health_probe_t probe = make_probe();

    assert(health_probe_due(&probe, 9) == 0);
    assert(health_probe_due(&probe, 10) == 1);

    probe.last_result = HEALTH_PROBE_RESULT_REFUSED;
    assert(health_probe_due(&probe, 1000) == 0);

    assert(health_probe_take_result(&probe, NULL, NULL)
        == HEALTH_PROBE_RESULT_REFUSED);
    probe.next_probe_ns = 50;
    assert(health_probe_due(&probe, 50) == 1);
    health_probe_destroy(&probe);
}

static void test_deadline_produces_timeout_without_blocking(void)
{
    health_probe_t probe = make_probe();
    int error_code = 0;
    uint64_t latency_ns = 0;

    probe.state = HEALTH_PROBE_CONNECTING;
    probe.fd = -1;
    probe.started_ns = 100;
    probe.deadline_ns = 120;

    assert(health_probe_poll(&probe, 119) == -1);

    probe.fd = -1;
    probe.state = HEALTH_PROBE_CONNECTING;
    assert(health_probe_poll(&probe, 120) == 1);
    assert(health_probe_has_result(&probe) == 1);
    assert(probe.timeouts == 1);
    assert(probe.failures == 1);
    assert(health_probe_take_result(&probe, &error_code, &latency_ns)
        == HEALTH_PROBE_RESULT_TIMEOUT);
    assert(error_code == ETIMEDOUT);
    assert(latency_ns == 20);
    assert(health_probe_has_result(&probe) == 0);
}

static void test_result_names(void)
{
    assert(health_probe_result_name(HEALTH_PROBE_RESULT_NONE)[0] == 'N');
    assert(health_probe_result_name(HEALTH_PROBE_RESULT_SUCCESS)[0] == 'S');
    assert(health_probe_result_name(HEALTH_PROBE_RESULT_REFUSED)[0] == 'R');
    assert(health_probe_result_name(HEALTH_PROBE_RESULT_TIMEOUT)[0] == 'T');
    assert(health_probe_result_name(HEALTH_PROBE_RESULT_ERROR)[0] == 'E');
}

int main(void)
{
    test_configuration_validation();
    test_due_schedule_requires_result_consumption();
    test_deadline_produces_timeout_without_blocking();
    test_result_names();
    puts("health probe tests passed");
    return 0;
}
