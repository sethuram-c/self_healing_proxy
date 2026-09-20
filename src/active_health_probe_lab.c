#define _POSIX_C_SOURCE 200809L

#include "backend_health.h"
#include "health_probe.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void sleep_ms(unsigned ms)
{
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000U);
    req.tv_nsec = (long)(ms % 1000U) * 1000000L;
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

static int parse_ulong(const char *text, unsigned long *out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || text[0] == '\0') {
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }
    *out = value;
    return 0;
}

static void usage(const char *argv0)
{
    printf(
        "Usage: %s --host IPV4 --port PORT [options]\n"
        "Options:\n"
        "  --cycles N              completed probes (default 4)\n"
        "  --interval-ms N         probe interval (default 50)\n"
        "  --timeout-ms N          per-probe deadline (default 100)\n"
        "  --failure-threshold N   failures before UNHEALTHY (default 2)\n"
        "  --recovery-successes N  successes to recover (default 2)\n"
        "  --cooldown-ms N         UNHEALTHY cooldown (default 30)\n"
        "  -h, --help              show help\n",
        argv0
    );
}

int main(int argc, char **argv)
{
    const char *host = NULL;
    unsigned long port = 0;
    unsigned long cycles = 4;
    unsigned long interval_ms = 50;
    unsigned long timeout_ms = 100;
    unsigned long failure_threshold = 2;
    unsigned long recovery_successes = 2;
    unsigned long cooldown_ms = 30;
    health_probe_t probe;
    backend_health_t health;
    health_probe_config_t probe_config;
    backend_health_config_t health_config;
    uint64_t first_ns;
    unsigned long completed = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (i + 1 >= argc) {
            usage(argv[0]);
            return 2;
        }
        if (strcmp(argv[i], "--host") == 0) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0) {
            if (parse_ulong(argv[++i], &port) != 0 || port == 0 || port > 65535) {
                return 2;
            }
        } else if (strcmp(argv[i], "--cycles") == 0) {
            if (parse_ulong(argv[++i], &cycles) != 0 || cycles == 0 || cycles > 10000) {
                return 2;
            }
        } else if (strcmp(argv[i], "--interval-ms") == 0) {
            if (parse_ulong(argv[++i], &interval_ms) != 0 || interval_ms == 0 || interval_ms > 60000) {
                return 2;
            }
        } else if (strcmp(argv[i], "--timeout-ms") == 0) {
            if (parse_ulong(argv[++i], &timeout_ms) != 0 || timeout_ms == 0 || timeout_ms > 60000) {
                return 2;
            }
        } else if (strcmp(argv[i], "--failure-threshold") == 0) {
            if (parse_ulong(argv[++i], &failure_threshold) != 0 || failure_threshold == 0) {
                return 2;
            }
        } else if (strcmp(argv[i], "--recovery-successes") == 0) {
            if (parse_ulong(argv[++i], &recovery_successes) != 0 || recovery_successes == 0) {
                return 2;
            }
        } else if (strcmp(argv[i], "--cooldown-ms") == 0) {
            if (parse_ulong(argv[++i], &cooldown_ms) != 0 || cooldown_ms == 0 || cooldown_ms > 60000) {
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (host == NULL || port == 0
        || interval_ms > UINT64_MAX / UINT64_C(1000000)
        || timeout_ms > UINT64_MAX / UINT64_C(1000000)
        || cooldown_ms > UINT64_MAX / UINT64_C(1000000)) {
        usage(argv[0]);
        return 2;
    }

    probe_config.interval_ns = (uint64_t)interval_ms * UINT64_C(1000000);
    probe_config.timeout_ns = (uint64_t)timeout_ms * UINT64_C(1000000);
    health_config.failure_threshold = (size_t)failure_threshold;
    health_config.recovery_success_threshold = (size_t)recovery_successes;
    health_config.cooldown_ns = (uint64_t)cooldown_ms * UINT64_C(1000000);

    first_ns = monotonic_ns();
    if (first_ns == 0
        || backend_health_init(&health, &health_config) != 0
        || health_probe_init(&probe, host, (uint16_t)port, &probe_config, first_ns) != 0) {
        fprintf(stderr, "initialization failed\n");
        return 1;
    }

    printf(
        "PHASE40_ACTIVE_PROBE_READY host=%s port=%lu cycles=%lu interval_ms=%lu timeout_ms=%lu\n",
        host, port, cycles, interval_ms, timeout_ms
    );
    fflush(stdout);

    while (completed < cycles) {
        uint64_t now_ns = monotonic_ns();
        if (now_ns == 0) {
            health_probe_destroy(&probe);
            return 1;
        }

        if (backend_health_tick(&health, now_ns) != 0) {
            health_probe_destroy(&probe);
            return 1;
        }

        if (health_probe_due(&probe, now_ns)) {
            if (health_probe_start(&probe, now_ns) != 0) {
                health_probe_destroy(&probe);
                return 1;
            }
        }

        if (probe.state == HEALTH_PROBE_CONNECTING) {
            if (health_probe_poll(&probe, now_ns) < 0) {
                health_probe_destroy(&probe);
                return 1;
            }
        }

        if (health_probe_has_result(&probe)) {
            int error_code = 0;
            uint64_t latency_ns = 0;
            health_probe_result_t result = health_probe_take_result(
                &probe, &error_code, &latency_ns
            );
            now_ns = monotonic_ns();
            if (now_ns == 0 || backend_health_tick(&health, now_ns) != 0) {
                health_probe_destroy(&probe);
                return 1;
            }

            if (result == HEALTH_PROBE_RESULT_SUCCESS) {
                if (backend_health_record_success(&health, now_ns) != 0) {
                    health_probe_destroy(&probe);
                    return 1;
                }
            } else {
                if (backend_health_record_failure(&health, now_ns) != 0) {
                    health_probe_destroy(&probe);
                    return 1;
                }
            }

            completed++;
            printf(
                "PROBE cycle=%lu result=%s errno=%d latency_ns=%" PRIu64
                " health=%s eligible=%d successes=%" PRIu64
                " failures=%" PRIu64 "\n",
                completed,
                health_probe_result_name(result),
                error_code,
                latency_ns,
                backend_health_state_name(health.state),
                backend_health_is_eligible(&health),
                health.total_successes,
                health.total_failures
            );
            fflush(stdout);
        }

        if (completed < cycles) {
            sleep_ms(1);
        }
    }

    health_probe_destroy(&probe);
    printf("PHASE40_ACTIVE_PROBE_OK\n");
    printf("PROBES=%" PRIu64 "\n", probe.attempts);
    printf("PROBE_SUCCESSES=%" PRIu64 "\n", probe.successes);
    printf("PROBE_FAILURES=%" PRIu64 "\n", probe.failures);
    printf("PROBE_TIMEOUTS=%" PRIu64 "\n", probe.timeouts);
    printf("FINAL_HEALTH=%s\n", backend_health_state_name(health.state));
    return 0;
}
