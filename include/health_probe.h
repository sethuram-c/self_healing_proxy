#ifndef HEALTH_PROBE_H
#define HEALTH_PROBE_H

#include <stddef.h>
#include <stdint.h>

#include <netinet/in.h>

typedef enum {
    HEALTH_PROBE_IDLE = 0,
    HEALTH_PROBE_CONNECTING
} health_probe_state_t;

typedef enum {
    HEALTH_PROBE_RESULT_NONE = 0,
    HEALTH_PROBE_RESULT_SUCCESS,
    HEALTH_PROBE_RESULT_REFUSED,
    HEALTH_PROBE_RESULT_TIMEOUT,
    HEALTH_PROBE_RESULT_ERROR
} health_probe_result_t;

typedef struct {
    uint64_t interval_ns;
    uint64_t timeout_ns;
} health_probe_config_t;

typedef struct {
    health_probe_state_t state;
    health_probe_config_t config;
    struct sockaddr_in address;
    int fd;
    uint64_t next_probe_ns;
    uint64_t started_ns;
    uint64_t deadline_ns;
    uint64_t completed_ns;
    health_probe_result_t last_result;
    int last_error;
    uint64_t attempts;
    uint64_t successes;
    uint64_t failures;
    uint64_t timeouts;
} health_probe_t;

int health_probe_init(
    health_probe_t *probe,
    const char *ipv4,
    uint16_t port,
    const health_probe_config_t *config,
    uint64_t first_probe_ns
);

void health_probe_destroy(health_probe_t *probe);

int health_probe_due(const health_probe_t *probe, uint64_t now_ns);

int health_probe_start(health_probe_t *probe, uint64_t now_ns);

int health_probe_poll(health_probe_t *probe, uint64_t now_ns);

int health_probe_has_result(const health_probe_t *probe);

health_probe_result_t health_probe_take_result(
    health_probe_t *probe,
    int *error_out,
    uint64_t *latency_ns_out
);

const char *health_probe_result_name(health_probe_result_t result);

#endif
