#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Phase 21: observe namespaces without creating or modifying isolation. */
static const char *const namespaces[] = {
    "user", "pid", "mnt", "net", "ipc", "uts", "cgroup", "time"
};

static int parse_pid(const char *text, pid_t *result)
{
    char *end = NULL;
    long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return -1;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0'
        || value <= 0 || value > INT_MAX) {
        return -1;
    }
    *result = (pid_t)value;
    return 0;
}

static int namespace_path(char *dest, size_t cap, pid_t pid,
                          const char *name)
{
    int size = snprintf(dest, cap, "/proc/%ld/ns/%s", (long)pid, name);
    if (size < 0 || (size_t)size >= cap) {
        return -1;
    }
    return 0;
}

static int inspect_namespace(pid_t self, pid_t target, const char *name)
{
    char self_path[128];
    char target_path[128];
    char self_link[128];
    char target_link[128];
    struct stat self_stat;
    struct stat target_stat;
    ssize_t n;

    if (namespace_path(self_path, sizeof(self_path), self, name) != 0
        || namespace_path(target_path, sizeof(target_path), target, name) != 0) {
        fprintf(stderr, "Namespace path too long: %s\n", name);
        return -1;
    }
    n = readlink(self_path, self_link, sizeof(self_link) - 1);
    if (n == -1) {
        if (errno == ENOENT) {
            printf("%-7s unavailable\n", name);
            return 0;
        }
        perror(self_path);
        return -1;
    }
    self_link[n] = '\0';
    n = readlink(target_path, target_link, sizeof(target_link) - 1);
    if (n == -1) {
        perror(target_path);
        return -1;
    }
    target_link[n] = '\0';
    if (stat(self_path, &self_stat) == -1) {
        perror(self_path);
        return -1;
    }
    if (stat(target_path, &target_stat) == -1) {
        perror(target_path);
        return -1;
    }
    printf("%-7s self=%s target=%s shared=%s\n", name,
           self_link, target_link,
           (self_stat.st_dev == target_stat.st_dev
            && self_stat.st_ino == target_stat.st_ino) ? "yes" : "no");
    return 0;
}

int main(int argc, char **argv)
{
    pid_t self = getpid();
    pid_t target = self;
    int failed = 0;

    if (argc > 2 || (argc == 2 && parse_pid(argv[1], &target) != 0)) {
        fprintf(stderr, "Usage: %s [positive-PID]\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* Fail early if the requested PID does not exist or is inaccessible. */
    char proc_path[128];
    struct stat proc_stat;
    int size = snprintf(proc_path, sizeof(proc_path), "/proc/%ld/ns", (long)target);
    if (size < 0 || (size_t)size >= sizeof(proc_path)
        || stat(proc_path, &proc_stat) == -1) {
        fprintf(stderr, "Cannot inspect PID %ld: %s\n", (long)target,
                size < 0 || (size_t)size >= sizeof(proc_path)
                    ? "invalid path" : strerror(errno));
        return EXIT_FAILURE;
    }
    printf("Observer PID=%ld PPID=%ld; target PID=%ld\n",
           (long)self, (long)getppid(), (long)target);
    for (size_t i = 0; i < sizeof(namespaces) / sizeof(namespaces[0]); ++i) {
        if (inspect_namespace(self, target, namespaces[i]) != 0) {
            failed = 1;
        }
    }
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
