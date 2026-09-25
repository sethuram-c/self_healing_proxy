#define _POSIX_C_SOURCE 200809L
#include "remediation_executor.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_worker = 0;

static void handle_term(int sig) {
    (void)sig;
    stop_worker = 1;
}

static int worker_main(const char *mode) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    if (strcmp(mode, "graceful") == 0) {
        sa.sa_handler = handle_term;
    } else if (strcmp(mode, "stubborn") == 0) {
        sa.sa_handler = SIG_IGN;
    } else {
        return 2;
    }
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return 3;
    }

    while (!stop_worker) {
        pause();
    }
    return 0;
}

static pid_t start_worker(const char *mode) {
    pid_t pid = fork();
    if (pid == 0) {
        _exit(worker_main(mode));
    }
    return pid;
}

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

static void cleanup_child(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        (void)kill(pid, SIGKILL);
    }
    for (int i = 0; i < 20; i++) {
        pid_t rc = waitpid(pid, NULL, WNOHANG);
        if (rc == pid || (rc < 0 && errno == ECHILD)) {
            return;
        }
        sleep_ms(10);
    }
    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, NULL, 0);
}

static int run_restart(const char *self, const char *old_mode, int expect_escalation) {
    remediation_executor_config_t config = { .term_grace_ms = 80U, .kill_grace_ms = 300U };
    remediation_executor_result_t result;
    process_identity_t old_identity;
    char *const replacement[] = { (char *)self, "--worker", "graceful", NULL };
    pid_t old_pid = start_worker(old_mode);

    if (old_pid <= 0) {
        perror("fork");
        return 1;
    }
    sleep_ms(20);
    if (process_identity_capture(old_pid, &old_identity) != 0) {
        perror("process_identity_capture");
        cleanup_child(old_pid);
        return 1;
    }

    if (remediation_executor_restart(&old_identity, replacement, &config, &result) != 0) {
        perror("remediation_executor_restart");
        cleanup_child(old_pid);
        return 1;
    }

    printf("PHASE45_EXECUTOR_RESULT status=%s old_pid=%ld new_pid=%ld escalated=%d\n",
           remediation_executor_status_name(result.status), (long)old_pid,
           (long)result.new_identity.pid, result.escalated_to_sigkill);

    if (result.status != REMEDIATION_EXECUTOR_RESTARTED ||
        result.new_identity.pid <= 0 || result.new_identity.pid == old_pid ||
        result.escalated_to_sigkill != expect_escalation ||
        process_identity_matches(&result.new_identity) != 1) {
        cleanup_child(result.new_identity.pid);
        return 1;
    }

    cleanup_child(result.new_identity.pid);
    printf("PHASE45_REMEDIATION_EXECUTOR_OK\n");
    return 0;
}

static int run_stale(const char *self) {
    remediation_executor_config_t config = { .term_grace_ms = 80U, .kill_grace_ms = 300U };
    remediation_executor_result_t result;
    process_identity_t identity;
    char *const replacement[] = { (char *)self, "--worker", "graceful", NULL };
    pid_t pid = start_worker("graceful");

    if (pid <= 0) return 1;
    sleep_ms(20);
    if (process_identity_capture(pid, &identity) != 0) {
        cleanup_child(pid);
        return 1;
    }
    identity.start_time_ticks++;

    if (remediation_executor_restart(&identity, replacement, &config, &result) != 0) {
        cleanup_child(pid);
        return 1;
    }

    printf("PHASE45_EXECUTOR_RESULT status=%s old_pid=%ld new_pid=%ld escalated=%d\n",
           remediation_executor_status_name(result.status), (long)pid,
           (long)result.new_identity.pid, result.escalated_to_sigkill);

    if (result.status != REMEDIATION_EXECUTOR_STALE_IDENTITY || kill(pid, 0) != 0) {
        cleanup_child(pid);
        return 1;
    }
    cleanup_child(pid);
    printf("PHASE45_REMEDIATION_EXECUTOR_OK\n");
    return 0;
}

static int run_spawn_fail(void) {
    remediation_executor_config_t config = { .term_grace_ms = 80U, .kill_grace_ms = 300U };
    remediation_executor_result_t result;
    process_identity_t identity;
    char *const replacement[] = { "/definitely/not/a/real/shp-phase45-command", NULL };
    pid_t pid = start_worker("graceful");

    if (pid <= 0) return 1;
    sleep_ms(20);
    if (process_identity_capture(pid, &identity) != 0) {
        cleanup_child(pid);
        return 1;
    }

    if (remediation_executor_restart(&identity, replacement, &config, &result) != 0) {
        cleanup_child(pid);
        return 1;
    }
    printf("PHASE45_EXECUTOR_RESULT status=%s spawn_errno=%d\n",
           remediation_executor_status_name(result.status), result.spawn_errno);
    if (result.status != REMEDIATION_EXECUTOR_SPAWN_FAILED || result.spawn_errno != ENOENT) {
        return 1;
    }
    printf("PHASE45_REMEDIATION_EXECUTOR_OK\n");
    return 0;
}

static void usage(const char *prog) {
    printf("Usage: %s --scenario graceful|escalate|stale|spawn-fail\n", prog);
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--worker") == 0) {
        return worker_main(argv[2]);
    }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(argv[0]);
        return 0;
    }
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "graceful") == 0) return run_restart(argv[0], "graceful", 0);
    if (strcmp(argv[2], "escalate") == 0) return run_restart(argv[0], "stubborn", 1);
    if (strcmp(argv[2], "stale") == 0) return run_stale(argv[0]);
    if (strcmp(argv[2], "spawn-fail") == 0) return run_spawn_fail();
    usage(argv[0]);
    return 2;
}
