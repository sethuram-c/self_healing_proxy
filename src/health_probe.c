#include "health_probe.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void close_fd(health_probe_t *probe)
{
    if (probe->fd >= 0) {
        close(probe->fd);
        probe->fd = -1;
    }
}

static void finish(
    health_probe_t *probe,
    health_probe_result_t result,
    int error_code,
    uint64_t now_ns
)
{
    close_fd(probe);
    probe->state = HEALTH_PROBE_IDLE;
    probe->completed_ns = now_ns;
    probe->last_result = result;
    probe->last_error = error_code;

    if (result == HEALTH_PROBE_RESULT_SUCCESS) {
        probe->successes++;
    } else {
        probe->failures++;
        if (result == HEALTH_PROBE_RESULT_TIMEOUT) {
            probe->timeouts++;
        }
    }

    if (UINT64_MAX - now_ns < probe->config.interval_ns) {
        probe->next_probe_ns = UINT64_MAX;
    } else {
        probe->next_probe_ns = now_ns + probe->config.interval_ns;
    }
}

static health_probe_result_t classify_error(int error_code)
{
    if (error_code == ECONNREFUSED) {
        return HEALTH_PROBE_RESULT_REFUSED;
    }
    if (error_code == ETIMEDOUT) {
        return HEALTH_PROBE_RESULT_TIMEOUT;
    }
    return HEALTH_PROBE_RESULT_ERROR;
}

int health_probe_init(
    health_probe_t *probe,
    const char *ipv4,
    uint16_t port,
    const health_probe_config_t *config,
    uint64_t first_probe_ns
)
{
    if (probe == NULL || ipv4 == NULL || config == NULL
        || port == 0 || config->interval_ns == 0 || config->timeout_ns == 0) {
        return -1;
    }

    memset(probe, 0, sizeof(*probe));
    probe->fd = -1;
    probe->config = *config;
    probe->address.sin_family = AF_INET;
    probe->address.sin_port = htons(port);
    probe->next_probe_ns = first_probe_ns;
    probe->last_result = HEALTH_PROBE_RESULT_NONE;

    if (inet_pton(AF_INET, ipv4, &probe->address.sin_addr) != 1) {
        return -1;
    }

    return 0;
}

void health_probe_destroy(health_probe_t *probe)
{
    if (probe == NULL) {
        return;
    }
    close_fd(probe);
    probe->state = HEALTH_PROBE_IDLE;
}

int health_probe_due(const health_probe_t *probe, uint64_t now_ns)
{
    if (probe == NULL || probe->state != HEALTH_PROBE_IDLE
        || probe->last_result != HEALTH_PROBE_RESULT_NONE) {
        return 0;
    }
    return now_ns >= probe->next_probe_ns;
}

int health_probe_start(health_probe_t *probe, uint64_t now_ns)
{
    int flags;
    int rc;

    if (probe == NULL || !health_probe_due(probe, now_ns)) {
        return -1;
    }

    probe->fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (probe->fd < 0) {
        finish(probe, HEALTH_PROBE_RESULT_ERROR, errno, now_ns);
        probe->attempts++;
        return 0;
    }

    flags = fcntl(probe->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(probe->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        int saved = errno;
        finish(probe, HEALTH_PROBE_RESULT_ERROR, saved, now_ns);
        probe->attempts++;
        return 0;
    }

    probe->attempts++;
    probe->started_ns = now_ns;
    if (UINT64_MAX - now_ns < probe->config.timeout_ns) {
        probe->deadline_ns = UINT64_MAX;
    } else {
        probe->deadline_ns = now_ns + probe->config.timeout_ns;
    }

    rc = connect(
        probe->fd,
        (const struct sockaddr *)&probe->address,
        sizeof(probe->address)
    );
    if (rc == 0) {
        finish(probe, HEALTH_PROBE_RESULT_SUCCESS, 0, now_ns);
        return 0;
    }

    if (errno == EINPROGRESS || errno == EALREADY || errno == EINTR) {
        probe->state = HEALTH_PROBE_CONNECTING;
        return 0;
    }

    {
        int saved = errno;
        finish(probe, classify_error(saved), saved, now_ns);
    }
    return 0;
}

int health_probe_poll(health_probe_t *probe, uint64_t now_ns)
{
    struct pollfd pfd;
    int rc;
    int socket_error = 0;
    socklen_t error_len = sizeof(socket_error);

    if (probe == NULL) {
        return -1;
    }
    if (probe->state != HEALTH_PROBE_CONNECTING) {
        return 0;
    }

    if (now_ns >= probe->deadline_ns) {
        finish(probe, HEALTH_PROBE_RESULT_TIMEOUT, ETIMEDOUT, now_ns);
        return 1;
    }

    if (probe->fd < 0) {
        return -1;
    }

    pfd.fd = probe->fd;
    pfd.events = POLLOUT | POLLERR | POLLHUP;
    pfd.revents = 0;

    rc = poll(&pfd, 1, 0);
    if (rc < 0) {
        if (errno == EINTR) {
            return 0;
        }
        finish(probe, HEALTH_PROBE_RESULT_ERROR, errno, now_ns);
        return 1;
    }
    if (rc == 0) {
        return 0;
    }

    if (getsockopt(
            probe->fd,
            SOL_SOCKET,
            SO_ERROR,
            &socket_error,
            &error_len) < 0) {
        finish(probe, HEALTH_PROBE_RESULT_ERROR, errno, now_ns);
        return 1;
    }

    if (socket_error == 0) {
        finish(probe, HEALTH_PROBE_RESULT_SUCCESS, 0, now_ns);
    } else {
        finish(
            probe,
            classify_error(socket_error),
            socket_error,
            now_ns
        );
    }
    return 1;
}

int health_probe_has_result(const health_probe_t *probe)
{
    return probe != NULL && probe->last_result != HEALTH_PROBE_RESULT_NONE;
}

health_probe_result_t health_probe_take_result(
    health_probe_t *probe,
    int *error_out,
    uint64_t *latency_ns_out
)
{
    health_probe_result_t result;

    if (!health_probe_has_result(probe)) {
        return HEALTH_PROBE_RESULT_NONE;
    }

    result = probe->last_result;
    if (error_out != NULL) {
        *error_out = probe->last_error;
    }
    if (latency_ns_out != NULL) {
        *latency_ns_out = probe->completed_ns >= probe->started_ns
            ? probe->completed_ns - probe->started_ns
            : 0;
    }

    probe->last_result = HEALTH_PROBE_RESULT_NONE;
    probe->last_error = 0;
    return result;
}

const char *health_probe_result_name(health_probe_result_t result)
{
    switch (result) {
    case HEALTH_PROBE_RESULT_NONE:
        return "NONE";
    case HEALTH_PROBE_RESULT_SUCCESS:
        return "SUCCESS";
    case HEALTH_PROBE_RESULT_REFUSED:
        return "REFUSED";
    case HEALTH_PROBE_RESULT_TIMEOUT:
        return "TIMEOUT";
    case HEALTH_PROBE_RESULT_ERROR:
        return "ERROR";
    default:
        return "INVALID";
    }
}
