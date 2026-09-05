#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_MIB 4096UL

static void usage(FILE *stream) {
    fprintf(stream,
            "Usage: memory_pressure --mib N [--wait-for-sigcont]\n"
            "\n"
            "Allocate and touch N MiB of anonymous memory. Touching every page forces\n"
            "the kernel to charge real memory to the process/cgroup instead of relying\n"
            "on virtual-address allocation alone.\n"
            "\n"
            "Options:\n"
            "  --mib N              Memory target in MiB (1..4096).\n"
            "  --wait-for-sigcont   Raise SIGSTOP before allocation; caller must SIGCONT.\n"
            "  --help               Show this help.\n");
}

static int parse_mib(const char *text, unsigned long *value_out) {
    char *end = NULL;
    unsigned long value;

    if (text == NULL || *text == '\0') {
        return -1;
    }

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 || value > MAX_MIB) {
        return -1;
    }

    *value_out = value;
    return 0;
}

int main(int argc, char **argv) {
    unsigned long mib = 0;
    int wait_for_sigcont = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }

        if (strcmp(argv[i], "--wait-for-sigcont") == 0) {
            wait_for_sigcont = 1;
            continue;
        }

        if (strcmp(argv[i], "--mib") == 0) {
            if (i + 1 >= argc || parse_mib(argv[i + 1], &mib) != 0) {
                fprintf(stderr, "invalid --mib value\n");
                return 2;
            }
            ++i;
            continue;
        }

        fprintf(stderr, "unknown argument: %s\n", argv[i]);
        usage(stderr);
        return 2;
    }

    if (mib == 0) {
        fprintf(stderr, "--mib is required\n");
        usage(stderr);
        return 2;
    }

    printf("MEMORY_PRESSURE_READY pid=%ld target_mib=%lu\n", (long)getpid(), mib);
    fflush(stdout);

    if (wait_for_sigcont) {
        if (raise(SIGSTOP) != 0) {
            perror("raise(SIGSTOP)");
            return 1;
        }
    }

    printf("MEMORY_PRESSURE_START target_mib=%lu\n", mib);
    fflush(stdout);

    if (mib > SIZE_MAX / (1024UL * 1024UL)) {
        fprintf(stderr, "requested allocation overflows size_t\n");
        return 2;
    }

    const size_t bytes = (size_t)mib * 1024UL * 1024UL;
    unsigned char *buffer = malloc(bytes);
    if (buffer == NULL) {
        perror("malloc");
        return 1;
    }

    long page_size_long = sysconf(_SC_PAGESIZE);
    if (page_size_long <= 0) {
        fprintf(stderr, "unable to determine system page size\n");
        free(buffer);
        return 1;
    }

    const size_t page_size = (size_t)page_size_long;
    size_t offset;
    size_t next_report = 8UL * 1024UL * 1024UL;

    for (offset = 0; offset < bytes; offset += page_size) {
        buffer[offset] = (unsigned char)(offset / page_size);

        if (offset >= next_report) {
            printf("MEMORY_PRESSURE_TOUCHED_MIB=%zu\n", offset / (1024UL * 1024UL));
            fflush(stdout);
            next_report += 8UL * 1024UL * 1024UL;
        }
    }

    if (bytes > 0) {
        buffer[bytes - 1] = 0xA5U;
    }

    printf("MEMORY_PRESSURE_COMPLETE mib=%lu\n", mib);
    fflush(stdout);

    free(buffer);
    return 0;
}
