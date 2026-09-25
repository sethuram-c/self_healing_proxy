#define _GNU_SOURCE
#include "remediation_executor.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_pidfd_open
#error "pidfd_open syscall number is required for Phase 45"
#endif
#ifndef SYS_pidfd_send_signal
#error "pidfd_send_signal syscall number is required for Phase 45"
#endif

static int pidfd_open_sys(pid_t pid) {
    return (int)syscall(SYS_pidfd_open, pid, 0U);
}

static int pidfd_send_signal_sys(int pidfd, int sig) {
    return (int)syscall(SYS_pidfd_send_signal, pidfd, sig, NULL, 0U);
}

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int wait_pidfd_bounded(int pidfd, unsigned int timeout_ms) {
    const uint64_t start = monotonic_ms();
    const uint64_t deadline = start + (uint64_t)timeout_ms;

    for (;;) {
        struct pollfd pfd;
        uint64_t now;
        int remaining;
        int rc;

        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = pidfd;
        pfd.events = POLLIN;

        now = monotonic_ms();
        if (now >= deadline) {
            remaining = 0;
        } else {
            const uint64_t delta = deadline - now;
            remaining = delta > (uint64_t)INT_MAX ? INT_MAX : (int)delta;
        }

        rc = poll(&pfd, 1, remaining);
        if (rc > 0) {
            return 1;
        }
        if (rc == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
}

static int parse_start_time(const char *stat_text, unsigned long long *value) {
    const char *rparen;
    char copy[4096];
    char *save = NULL;
    char *token;
    size_t token_index = 0;

    if (stat_text == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }

    rparen = strrchr(stat_text, ')');
    if (rparen == NULL || rparen[1] != ' ') {
        errno = EPROTO;
        return -1;
    }

    if (strlen(rparen + 2) >= sizeof(copy)) {
        errno = EOVERFLOW;
        return -1;
    }
    strcpy(copy, rparen + 2);

    token = strtok_r(copy, " ", &save);
    while (token != NULL) {
        token_index++;
        /* Tokens after comm begin at proc field 3; field 22 is token 20. */
        if (token_index == 20U) {
            char *end = NULL;
            unsigned long long parsed;
            errno = 0;
            parsed = strtoull(token, &end, 10);
            if (errno != 0 || end == token || (*end != '\0' && *end != '\n')) {
                errno = EPROTO;
                return -1;
            }
            *value = parsed;
            return 0;
        }
        token = strtok_r(NULL, " ", &save);
    }

    errno = EPROTO;
    return -1;
}

int process_identity_capture(pid_t pid, process_identity_t *identity) {
    char path[64];
    char buffer[4096];
    int fd;
    ssize_t n;
    unsigned long long start_time;

    if (pid <= 0 || identity == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid) >= (int)sizeof(path)) {
        errno = EOVERFLOW;
        return -1;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    n = read(fd, buffer, sizeof(buffer) - 1U);
    {
        const int saved = errno;
        close(fd);
        errno = saved;
    }
    if (n <= 0) {
        if (n == 0) {
            errno = EIO;
        }
        return -1;
    }
    buffer[n] = '\0';

    if (parse_start_time(buffer, &start_time) != 0) {
        return -1;
    }

    identity->pid = pid;
    identity->start_time_ticks = start_time;
    return 0;
}

int process_identity_matches(const process_identity_t *identity) {
    process_identity_t current;

    if (identity == NULL || identity->pid <= 0) {
        errno = EINVAL;
        return -1;
    }
    if (process_identity_capture(identity->pid, &current) != 0) {
        if (errno == ENOENT || errno == ESRCH) {
            return 0;
        }
        return -1;
    }
    return current.start_time_ticks == identity->start_time_ticks ? 1 : 0;
}

static int spawn_replacement(char *const argv[], process_identity_t *identity, int *spawn_errno) {
    int pipefd[2];
    pid_t pid;
    int child_errno = 0;
    ssize_t n;

    if (argv == NULL || argv[0] == NULL || identity == NULL || spawn_errno == NULL) {
        errno = EINVAL;
        return -1;
    }
    *spawn_errno = 0;

    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        const int saved = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        errno = saved;
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        execvp(argv[0], argv);
        child_errno = errno;
        (void)write(pipefd[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }

    close(pipefd[1]);
    do {
        n = read(pipefd[0], &child_errno, sizeof(child_errno));
    } while (n < 0 && errno == EINTR);
    close(pipefd[0]);

    if (n > 0) {
        int status;
        (void)waitpid(pid, &status, 0);
        *spawn_errno = child_errno;
        errno = child_errno;
        return -1;
    }
    if (n < 0) {
        const int saved = errno;
        (void)kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        errno = saved;
        return -1;
    }

    if (process_identity_capture(pid, identity) != 0) {
        const int saved = errno;
        (void)kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        errno = saved;
        return -1;
    }
    return 0;
}

int remediation_executor_restart(
    const process_identity_t *expected,
    char *const replacement_argv[],
    const remediation_executor_config_t *config,
    remediation_executor_result_t *result
) {
    int match;
    int pidfd;
    int wait_rc;
    int status = 0;
    process_identity_t current;

    if (expected == NULL || expected->pid <= 0 || replacement_argv == NULL || replacement_argv[0] == NULL ||
        config == NULL || config->term_grace_ms == 0U || config->kill_grace_ms == 0U || result == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(result, 0, sizeof(*result));
    result->old_identity = *expected;
    result->new_identity.pid = -1;

    match = process_identity_matches(expected);
    if (match < 0) {
        return -1;
    }
    if (match == 0) {
        result->status = REMEDIATION_EXECUTOR_STALE_IDENTITY;
        return 0;
    }

    pidfd = pidfd_open_sys(expected->pid);
    if (pidfd < 0) {
        if (errno == ESRCH) {
            result->status = REMEDIATION_EXECUTOR_TARGET_GONE;
            return 0;
        }
        return -1;
    }

    /* Verify again after acquiring the stable pidfd reference. */
    if (process_identity_capture(expected->pid, &current) != 0 ||
        current.start_time_ticks != expected->start_time_ticks) {
        close(pidfd);
        result->status = REMEDIATION_EXECUTOR_STALE_IDENTITY;
        return 0;
    }

    if (pidfd_send_signal_sys(pidfd, SIGTERM) != 0) {
        if (errno == ESRCH) {
            close(pidfd);
            result->status = REMEDIATION_EXECUTOR_TARGET_GONE;
            return 0;
        }
        close(pidfd);
        result->status = REMEDIATION_EXECUTOR_SIGNAL_FAILED;
        return 0;
    }

    wait_rc = wait_pidfd_bounded(pidfd, config->term_grace_ms);
    if (wait_rc < 0) {
        close(pidfd);
        return -1;
    }

    if (wait_rc == 0) {
        result->escalated_to_sigkill = 1;
        if (pidfd_send_signal_sys(pidfd, SIGKILL) != 0 && errno != ESRCH) {
            close(pidfd);
            result->status = REMEDIATION_EXECUTOR_SIGNAL_FAILED;
            return 0;
        }
        wait_rc = wait_pidfd_bounded(pidfd, config->kill_grace_ms);
        if (wait_rc <= 0) {
            close(pidfd);
            result->status = REMEDIATION_EXECUTOR_TERMINATION_TIMEOUT;
            return 0;
        }
    }

    close(pidfd);

    if (waitpid(expected->pid, &status, WNOHANG) == expected->pid) {
        result->old_wait_status = status;
    }

    if (spawn_replacement(replacement_argv, &result->new_identity, &result->spawn_errno) != 0) {
        result->status = REMEDIATION_EXECUTOR_SPAWN_FAILED;
        return 0;
    }

    result->status = REMEDIATION_EXECUTOR_RESTARTED;
    return 0;
}

const char *remediation_executor_status_name(remediation_executor_status_t status) {
    switch (status) {
        case REMEDIATION_EXECUTOR_RESTARTED: return "RESTARTED";
        case REMEDIATION_EXECUTOR_STALE_IDENTITY: return "STALE_IDENTITY";
        case REMEDIATION_EXECUTOR_TARGET_GONE: return "TARGET_GONE";
        case REMEDIATION_EXECUTOR_SIGNAL_FAILED: return "SIGNAL_FAILED";
        case REMEDIATION_EXECUTOR_TERMINATION_TIMEOUT: return "TERMINATION_TIMEOUT";
        case REMEDIATION_EXECUTOR_SPAWN_FAILED: return "SPAWN_FAILED";
        default: return "UNKNOWN";
    }
}
