#define _POSIX_C_SOURCE 200809L
#include "recovery_orchestrator.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NS_PER_MS 1000000ULL

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void sleep_ms(unsigned int ms)
{
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000U);
    req.tv_nsec = (long)(ms % 1000U) * 1000000L;
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

static int reserve_port(void)
{
    int fd;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int port;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0
        || getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
        close(fd);
        return -1;
    }
    port = (int)ntohs(addr.sin_port);
    close(fd);
    return port;
}

static int run_worker(int port, unsigned int ready_delay_ms)
{
    int fd;
    int one = 1;
    struct sockaddr_in addr;

    sleep_ms(ready_delay_ms);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 2;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 16) != 0) {
        close(fd);
        return 3;
    }
    for (;;) {
        int client = accept(fd, NULL, NULL);
        if (client >= 0) {
            close(client);
            continue;
        }
        if (errno == EINTR) continue;
        close(fd);
        return 4;
    }
}

static int run_old_worker(void)
{
    for (;;) {
        pause();
    }
    return 0;
}

static pid_t spawn_old_worker(const char *self)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl(self, self, "--old-worker", (char *)NULL);
        _exit(127);
    }
    return pid;
}

static void cleanup_pid(pid_t pid)
{
    if (pid <= 0) return;
    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, NULL, 0);
}

static int prime_failure_state(
    backend_health_t *health,
    circuit_breaker_t *breaker,
    uint64_t now
)
{
    backend_health_config_t hc = {2, 2, 5 * NS_PER_MS};
    circuit_breaker_config_t cc = {2, 100 * NS_PER_MS, 5 * NS_PER_MS};
    uint64_t t1 = now - 20 * NS_PER_MS;
    uint64_t t2 = now - 10 * NS_PER_MS;

    if (backend_health_init(health, &hc) != 0
        || circuit_breaker_init(breaker, &cc) != 0
        || backend_health_record_failure(health, t1) != 0
        || backend_health_record_failure(health, t2) != 0
        || circuit_breaker_record_failure(breaker, t1) != 0
        || circuit_breaker_record_failure(breaker, t2) != 0) {
        return -1;
    }
    return 0;
}

static int supervise_replacement(
    const remediation_executor_result_t *rr,
    int port,
    uint64_t startup_timeout_ns,
    recovery_orchestrator_t *orchestrator,
    backend_health_t *health,
    circuit_breaker_t *breaker
)
{
    recovery_orchestrator_config_t oc = {startup_timeout_ns, 2};
    health_probe_config_t pc = {10 * NS_PER_MS, 20 * NS_PER_MS};
    health_probe_t probe;
    uint64_t now = monotonic_ns();

    if (recovery_orchestrator_init(orchestrator, &oc) != 0
        || recovery_orchestrator_begin(orchestrator, rr, health, breaker, now) != 0
        || health_probe_init(&probe, "127.0.0.1", (uint16_t)port, &pc, now) != 0) {
        return -1;
    }

    while (orchestrator->state == RECOVERY_ORCHESTRATOR_QUARANTINED) {
        now = monotonic_ns();
        if (recovery_orchestrator_tick(orchestrator, health, breaker, now) != 0) {
            health_probe_destroy(&probe);
            return -1;
        }
        if (orchestrator->state != RECOVERY_ORCHESTRATOR_QUARANTINED) break;

        if (health_probe_due(&probe, now)) {
            if (health_probe_start(&probe, now) != 0) {
                health_probe_destroy(&probe);
                return -1;
            }
        }
        if (probe.state == HEALTH_PROBE_CONNECTING) {
            if (health_probe_poll(&probe, now) < 0) {
                health_probe_destroy(&probe);
                return -1;
            }
        }
        if (health_probe_has_result(&probe)) {
            int probe_errno = 0;
            uint64_t latency = 0;
            health_probe_result_t result = health_probe_take_result(&probe, &probe_errno, &latency);
            printf("PROBE result=%s errno=%d latency_ns=%llu\n",
                   health_probe_result_name(result), probe_errno,
                   (unsigned long long)latency);
            if (recovery_orchestrator_record_probe(
                    orchestrator, result, health, breaker, monotonic_ns()) != 0) {
                health_probe_destroy(&probe);
                return -1;
            }
        }
        sleep_ms(2);
    }

    health_probe_destroy(&probe);
    return 0;
}

static int run_recovery_scenario(const char *self, int should_timeout)
{
    int port = reserve_port();
    pid_t old_pid;
    process_identity_t old_identity;
    remediation_executor_config_t ec = {100, 100};
    remediation_executor_result_t rr;
    backend_health_t health;
    circuit_breaker_t breaker;
    recovery_orchestrator_t orchestrator;
    char port_text[16];
    char delay_text[16];
    char *replacement_argv[7];
    uint64_t now;
    int rc = 1;

    if (port <= 0) return 1;
    old_pid = spawn_old_worker(self);
    if (old_pid <= 0) return 1;
    sleep_ms(20);
    if (process_identity_capture(old_pid, &old_identity) != 0) {
        cleanup_pid(old_pid);
        return 1;
    }

    snprintf(port_text, sizeof(port_text), "%d", port);
    snprintf(delay_text, sizeof(delay_text), "%u", should_timeout ? 400U : 40U);
    replacement_argv[0] = (char *)self;
    replacement_argv[1] = "--worker";
    replacement_argv[2] = "--port";
    replacement_argv[3] = port_text;
    replacement_argv[4] = "--ready-delay-ms";
    replacement_argv[5] = delay_text;
    replacement_argv[6] = NULL;

    if (remediation_executor_restart(&old_identity, replacement_argv, &ec, &rr) != 0) {
        cleanup_pid(old_pid);
        return 1;
    }
    printf("RESTART status=%s old_pid=%ld new_pid=%ld\n",
           remediation_executor_status_name(rr.status),
           (long)old_identity.pid, (long)rr.new_identity.pid);
    if (rr.status != REMEDIATION_EXECUTOR_RESTARTED) return 1;

    now = monotonic_ns();
    if (prime_failure_state(&health, &breaker, now) != 0) {
        cleanup_pid(rr.new_identity.pid);
        return 1;
    }

    if (supervise_replacement(
            &rr, port,
            (should_timeout ? 100ULL : 300ULL) * NS_PER_MS,
            &orchestrator, &health, &breaker) != 0) {
        cleanup_pid(rr.new_identity.pid);
        return 1;
    }

    printf("PHASE46_RECOVERY_RESULT state=%s reason=%s can_route=%d probes=%llu successes=%llu failures=%llu health=%s circuit=%s\n",
           recovery_orchestrator_state_name(orchestrator.state),
           recovery_failure_reason_name(orchestrator.failure_reason),
           recovery_orchestrator_can_route(&orchestrator),
           (unsigned long long)orchestrator.readiness_attempts,
           (unsigned long long)orchestrator.readiness_successes,
           (unsigned long long)orchestrator.readiness_failures,
           backend_health_state_name(health.state),
           circuit_breaker_state_name(breaker.state));

    if (!should_timeout) {
        rc = orchestrator.state == RECOVERY_ORCHESTRATOR_READY
            && recovery_orchestrator_can_route(&orchestrator)
            && health.state == BACKEND_HEALTH_HEALTHY
            && breaker.state == CIRCUIT_BREAKER_CLOSED ? 0 : 1;
    } else {
        rc = orchestrator.state == RECOVERY_ORCHESTRATOR_FAILED
            && orchestrator.failure_reason == RECOVERY_FAILURE_STARTUP_TIMEOUT
            && !recovery_orchestrator_can_route(&orchestrator) ? 0 : 1;
    }

    cleanup_pid(rr.new_identity.pid);
    if (rc == 0) puts("PHASE46_SUPERVISED_RECOVERY_OK");
    return rc;
}

static int run_restart_fail(void)
{
    recovery_orchestrator_t o;
    recovery_orchestrator_config_t oc = {100 * NS_PER_MS, 2};
    remediation_executor_result_t rr;
    backend_health_t health;
    circuit_breaker_t breaker;
    uint64_t now = monotonic_ns();

    memset(&rr, 0, sizeof(rr));
    rr.status = REMEDIATION_EXECUTOR_SPAWN_FAILED;
    rr.new_identity.pid = -1;
    if (prime_failure_state(&health, &breaker, now) != 0
        || recovery_orchestrator_init(&o, &oc) != 0
        || recovery_orchestrator_begin(&o, &rr, &health, &breaker, now) != 0) {
        return 1;
    }
    printf("PHASE46_RECOVERY_RESULT state=%s reason=%s can_route=%d\n",
           recovery_orchestrator_state_name(o.state),
           recovery_failure_reason_name(o.failure_reason),
           recovery_orchestrator_can_route(&o));
    if (o.state != RECOVERY_ORCHESTRATOR_FAILED
        || o.failure_reason != RECOVERY_FAILURE_RESTART_NOT_COMPLETED
        || recovery_orchestrator_can_route(&o)) return 1;
    puts("PHASE46_SUPERVISED_RECOVERY_OK");
    return 0;
}

static void usage(const char *argv0)
{
    printf("Usage: %s --scenario ready|timeout|restart-fail\n", argv0);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--old-worker") == 0) {
        return run_old_worker();
    }
    if (argc == 6 && strcmp(argv[1], "--worker") == 0
        && strcmp(argv[2], "--port") == 0
        && strcmp(argv[4], "--ready-delay-ms") == 0) {
        char *end1 = NULL;
        char *end2 = NULL;
        long port = strtol(argv[3], &end1, 10);
        unsigned long delay = strtoul(argv[5], &end2, 10);
        if (end1 == argv[3] || *end1 != '\0' || end2 == argv[5] || *end2 != '\0'
            || port <= 0 || port > 65535 || delay > 60000UL) return 2;
        return run_worker((int)port, (unsigned int)delay);
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (argc != 3 || strcmp(argv[1], "--scenario") != 0) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "ready") == 0) return run_recovery_scenario(argv[0], 0);
    if (strcmp(argv[2], "timeout") == 0) return run_recovery_scenario(argv[0], 1);
    if (strcmp(argv[2], "restart-fail") == 0) return run_restart_fail();
    usage(argv[0]);
    return 2;
}
