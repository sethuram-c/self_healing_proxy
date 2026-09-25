#ifndef REMEDIATION_EXECUTOR_H
#define REMEDIATION_EXECUTOR_H

#include <stdint.h>
#include <sys/types.h>

typedef struct {
    pid_t pid;
    unsigned long long start_time_ticks;
} process_identity_t;

typedef struct {
    unsigned int term_grace_ms;
    unsigned int kill_grace_ms;
} remediation_executor_config_t;

typedef enum {
    REMEDIATION_EXECUTOR_RESTARTED = 0,
    REMEDIATION_EXECUTOR_STALE_IDENTITY,
    REMEDIATION_EXECUTOR_TARGET_GONE,
    REMEDIATION_EXECUTOR_SIGNAL_FAILED,
    REMEDIATION_EXECUTOR_TERMINATION_TIMEOUT,
    REMEDIATION_EXECUTOR_SPAWN_FAILED
} remediation_executor_status_t;

typedef struct {
    remediation_executor_status_t status;
    process_identity_t old_identity;
    process_identity_t new_identity;
    int escalated_to_sigkill;
    int old_wait_status;
    int spawn_errno;
} remediation_executor_result_t;

int process_identity_capture(pid_t pid, process_identity_t *identity);
int process_identity_matches(const process_identity_t *identity);
int remediation_executor_restart(
    const process_identity_t *expected,
    char *const replacement_argv[],
    const remediation_executor_config_t *config,
    remediation_executor_result_t *result
);
const char *remediation_executor_status_name(remediation_executor_status_t status);

#endif
