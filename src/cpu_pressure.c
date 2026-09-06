#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_DURATION_MS 600000ULL

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage: cpu_pressure --duration-ms N [--wait-for-sigcont]\n"
            "Burns CPU for N milliseconds using CLOCK_MONOTONIC.\n"
            "--wait-for-sigcont stops with SIGSTOP before the burn loop.\n");
}

static int parse_u64(const char *text, uint64_t *value_out)
{
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || *text == '\0' || value_out == NULL) {
        return -1;
    }

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    *value_out = (uint64_t)value;
    return 0;
}

static uint64_t monotonic_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv)
{
    uint64_t duration_ms = 0;
    int wait_for_sigcont = 0;
    int have_duration = 0;
    int i;
    uint64_t start_ns;
    uint64_t deadline_ns;
    volatile uint64_t accumulator = 0x9e3779b97f4a7c15ULL;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }

        if (strcmp(argv[i], "--duration-ms") == 0) {
            if (i + 1 >= argc || parse_u64(argv[i + 1], &duration_ms) != 0 ||
                duration_ms == 0 || duration_ms > MAX_DURATION_MS) {
                fprintf(stderr, "invalid --duration-ms value\n");
                return 2;
            }
            have_duration = 1;
            ++i;
            continue;
        }

        if (strcmp(argv[i], "--wait-for-sigcont") == 0) {
            wait_for_sigcont = 1;
            continue;
        }

        fprintf(stderr, "unknown argument: %s\n", argv[i]);
        usage(stderr);
        return 2;
    }

    if (!have_duration) {
        fprintf(stderr, "--duration-ms is required\n");
        usage(stderr);
        return 2;
    }

    printf("CPU_PRESSURE_READY pid=%ld duration_ms=%llu\n",
           (long)getpid(), (unsigned long long)duration_ms);
    fflush(stdout);

    if (wait_for_sigcont) {
        if (raise(SIGSTOP) != 0) {
            perror("raise(SIGSTOP)");
            return 1;
        }
    }

    printf("CPU_PRESSURE_START duration_ms=%llu\n",
           (unsigned long long)duration_ms);
    fflush(stdout);

    start_ns = monotonic_ns();
    deadline_ns = start_ns + (duration_ms * 1000000ULL);

    while (monotonic_ns() < deadline_ns) {
        accumulator ^= accumulator << 7;
        accumulator ^= accumulator >> 9;
        accumulator *= 0x2545f4914f6cdd1dULL;
        accumulator += 0x9e3779b97f4a7c15ULL;
    }

    printf("CPU_PRESSURE_COMPLETE duration_ms=%llu checksum=%llu\n",
           (unsigned long long)duration_ms,
           (unsigned long long)accumulator);
    fflush(stdout);
    return 0;
}
