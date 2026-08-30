#define _POSIX_C_SOURCE 200809L

#include "backend_pool.h"
#include "connection.h"
#include "http_framing.h"
#include "net_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 8081
#define BACKLOG 128
#define MAX_EVENTS 64
#define READ_CHUNK 1024

#define DEFAULT_CONNECT_TIMEOUT_MS 3000ULL
#define DEFAULT_IDLE_TIMEOUT_MS 30000ULL
#define DEFAULT_REQUEST_TIMEOUT_MS 10000ULL
#define DEFAULT_RESPONSE_TIMEOUT_MS 10000ULL
#define MAX_TIMEOUT_MS 3600000ULL
#define DEFAULT_BACKEND_MAX_CONNECTIONS 64U
#define MAX_BACKEND_CONNECTIONS 100000U
#define DEFAULT_MAX_BACKEND_RETRIES 1U

#define BUFFER_HIGH_WATERMARK \
    ((IO_BUFFER_CAPACITY * 3U) / 4U)

#define BUFFER_LOW_WATERMARK \
    (IO_BUFFER_CAPACITY / 4U)

static volatile sig_atomic_t stop_requested = 0;

static uint64_t next_connection_id = 1;

static connection_t *connections = NULL;
static connection_t *garbage_connections = NULL;

static size_t total_connections = 0;
static size_t active_connections = 0;
static size_t peak_connections = 0;
static size_t total_bytes_received = 0;
static size_t total_timeouts = 0;
static size_t total_admission_rejections = 0;
static size_t total_backend_retries = 0;
static size_t total_retry_suppressions = 0;

static size_t max_backend_retries =
    DEFAULT_MAX_BACKEND_RETRIES;

static size_t backend_max_connections =
    DEFAULT_BACKEND_MAX_CONNECTIONS;

static backend_target_t backend_targets[] = {
    {"backend-a", "127.0.0.1", 9001, 0, 1},
    {"backend-b", "127.0.0.1", 9002, 0, 1}
};

#define BACKEND_COUNT \
    (sizeof(backend_targets) / sizeof(backend_targets[0]))

static backend_pool_t backend_pool;

typedef struct {
    uint64_t connect_ms;
    uint64_t idle_ms;
    uint64_t request_ms;
    uint64_t response_ms;
} timeout_config_t;

static timeout_config_t timeout_config = {
    DEFAULT_CONNECT_TIMEOUT_MS,
    DEFAULT_IDLE_TIMEOUT_MS,
    DEFAULT_REQUEST_TIMEOUT_MS,
    DEFAULT_RESPONSE_TIMEOUT_MS
};

static uint64_t monotonic_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        perror("clock_gettime");
        return 1;
    }

    return ((uint64_t)now.tv_sec * 1000ULL)
        + ((uint64_t)now.tv_nsec / 1000000ULL);
}

static uint64_t elapsed_ms(
    uint64_t now_ms,
    uint64_t started_ms
)
{
    if (now_ms < started_ms) {
        return 0;
    }

    return now_ms - started_ms;
}

static int parse_timeout_env(
    const char *name,
    uint64_t *value
)
{
    const char *text = getenv(name);

    if (text == NULL || *text == '\0') {
        return 0;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(
        text,
        &end,
        10
    );

    if (errno != 0
        || end == text
        || *end != '\0'
        || parsed == 0
        || parsed > MAX_TIMEOUT_MS) {

        fprintf(
            stderr,
            "Invalid %s=%s; expected 1..%llu milliseconds\n",
            name,
            text,
            (unsigned long long)MAX_TIMEOUT_MS
        );

        return -1;
    }

    *value = (uint64_t)parsed;
    return 0;
}

static int parse_backend_capacity_env(void)
{
    const char *text = getenv(
        "MICROPROXY_BACKEND_MAX_CONNECTIONS"
    );

    if (text == NULL || *text == '\0') {
        return 0;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(
        text,
        &end,
        10
    );

    if (errno != 0
        || end == text
        || *end != '\0'
        || parsed == 0
        || parsed > MAX_BACKEND_CONNECTIONS) {

        fprintf(
            stderr,
            "Invalid MICROPROXY_BACKEND_MAX_CONNECTIONS=%s; "
            "expected 1..%u\n",
            text,
            MAX_BACKEND_CONNECTIONS
        );

        return -1;
    }

    backend_max_connections = (size_t)parsed;
    return 0;
}

static int load_timeout_config(void)
{
    if (parse_timeout_env(
            "MICROPROXY_CONNECT_TIMEOUT_MS",
            &timeout_config.connect_ms) == -1) {
        return -1;
    }

    if (parse_timeout_env(
            "MICROPROXY_IDLE_TIMEOUT_MS",
            &timeout_config.idle_ms) == -1) {
        return -1;
    }

    if (parse_timeout_env(
            "MICROPROXY_REQUEST_TIMEOUT_MS",
            &timeout_config.request_ms) == -1) {
        return -1;
    }

    if (parse_timeout_env(
            "MICROPROXY_RESPONSE_TIMEOUT_MS",
            &timeout_config.response_ms) == -1) {
        return -1;
    }

    return 0;
}

static void note_activity(connection_t *connection)
{
    connection->last_activity_ms = monotonic_ms();
}

static void queue_connection_for_free(
    connection_t *connection
)
{
    connection->garbage_next =
        garbage_connections;

    garbage_connections = connection;
}

static void free_destroyed_connections(void)
{
    while (garbage_connections != NULL) {
        connection_t *connection =
            garbage_connections;

        garbage_connections =
            connection->garbage_next;

        connection->garbage_next = NULL;

        connection_free(connection);
    }
}

static void handle_signal(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;

    if (sigemptyset(&action.sa_mask) == -1) {
        return -1;
    }

    if (sigaction(SIGINT, &action, NULL) == -1) {
        return -1;
    }

    if (sigaction(SIGTERM, &action, NULL) == -1) {
        return -1;
    }

    return 0;
}

static int create_listener(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        return -1;
    }

    int enable = 1;

    if (setsockopt(
            fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &enable,
            sizeof(enable)) == -1) {

        close(fd);
        return -1;
    }

    struct sockaddr_in address;

    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(PORT);

    if (bind(
            fd,
            (struct sockaddr *)&address,
            sizeof(address)) == -1) {

        close(fd);
        return -1;
    }

    if (listen(fd, BACKLOG) == -1) {
        close(fd);
        return -1;
    }

    if (set_nonblocking(fd) == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

static int add_event_source(
    int epoll_fd,
    int fd,
    uint32_t events,
    event_source_t *source
)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));

    event.events = events;
    event.data.ptr = source;

    return epoll_ctl(
        epoll_fd,
        EPOLL_CTL_ADD,
        fd,
        &event
    );
}

static int modify_event_source(
    int epoll_fd,
    int fd,
    uint32_t events,
    event_source_t *source
)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));

    event.events = events;
    event.data.ptr = source;

    return epoll_ctl(
        epoll_fd,
        EPOLL_CTL_MOD,
        fd,
        &event
    );
}

static void connection_list_add(
    connection_t *connection
)
{
    connection->prev = NULL;
    connection->next = connections;

    if (connections != NULL) {
        connections->prev = connection;
    }

    connections = connection;
}

static void connection_list_remove(
    connection_t *connection
)
{
    if (connection->prev != NULL) {
        connection->prev->next =
            connection->next;
    } else {
        connections = connection->next;
    }

    if (connection->next != NULL) {
        connection->next->prev =
            connection->prev;
    }

    connection->prev = NULL;
    connection->next = NULL;
}

static void release_backend_slot(
    connection_t *connection
)
{
    if (!connection->backend_slot_acquired) {
        return;
    }

    if (backend_pool_release(
            &backend_pool,
            connection->backend_index) == -1) {

        fprintf(
            stderr,
            "Connection %llu backend slot release failed "
            "for index %zu\n",
            (unsigned long long)connection->id,
            connection->backend_index
        );
    }

    connection->backend_slot_acquired = 0;
}

static void close_backend(
    int epoll_fd,
    connection_t *connection
)
{
    if (connection->backend_fd == -1) {
        return;
    }

    if (epoll_ctl(
            epoll_fd,
            EPOLL_CTL_DEL,
            connection->backend_fd,
            NULL) == -1) {

        if (errno != ENOENT
            && errno != EBADF) {

            perror("epoll_ctl DEL backend");
        }
    }

    if (close(connection->backend_fd) == -1) {
        perror("close backend");
    }

    connection->backend_fd = -1;
    release_backend_slot(connection);
}

static void connection_destroy(
    int epoll_fd,
    connection_t *connection
)
{
    if (connection == NULL
        || connection->destroyed) {

        return;
    }

    connection->destroyed = 1;

    uint64_t id = connection->id;
    int fd = connection->fd;
    size_t bytes = connection->bytes_received;
    size_t requests = connection->requests_received;
    size_t to_backend =
        connection->bytes_forwarded_to_backend;
    size_t from_backend =
        connection->bytes_forwarded_to_client;

    total_bytes_received += bytes;

    if (active_connections > 0) {
        active_connections--;
    }

    if (epoll_ctl(
            epoll_fd,
            EPOLL_CTL_DEL,
            fd,
            NULL) == -1) {

        if (errno != ENOENT
            && errno != EBADF) {

            perror("epoll_ctl DEL client");
        }
    }

    close_backend(epoll_fd, connection);
    release_backend_slot(connection);
    connection_list_remove(connection);

    if (close(fd) == -1) {
        perror("close client");
    }

    queue_connection_for_free(connection);

    printf(
        "Connection %llu FD %d closed after "
        "%zu client bytes, %zu HTTP requests, "
        "%zu bytes to backend, %zu bytes to client, "
        "client pauses=%zu/%zu, backend pauses=%zu/%zu\n",
        (unsigned long long)id,
        fd,
        bytes,
        requests,
        to_backend,
        from_backend,
        connection->client_pause_count,
        connection->client_resume_count,
        connection->backend_pause_count,
        connection->backend_resume_count
    );
}

static void destroy_all_connections(
    int epoll_fd
)
{
    while (connections != NULL) {
        connection_destroy(
            epoll_fd,
            connections
        );
    }
}

static int update_client_interest(
    int epoll_fd,
    connection_t *connection
)
{
    if (connection->destroyed) {
        return 0;
    }

    uint32_t events =
        EPOLLERR | EPOLLHUP | EPOLLRDHUP;

    if (!connection->client_read_eof
        && !connection->client_read_paused) {

        events |= EPOLLIN;
    }

    if (io_buffer_size(
            &connection->backend_to_client) > 0) {

        events |= EPOLLOUT;
    }

    return modify_event_source(
        epoll_fd,
        connection->fd,
        events,
        &connection->event_source
    );
}


static int update_backend_interest(
    int epoll_fd,
    connection_t *connection
)
{
    if (connection->backend_fd == -1) {
        return 0;
    }

    uint32_t events = EPOLLERR | EPOLLHUP;

    if (connection->backend_state
        == BACKEND_CONNECTING) {

        events |= EPOLLOUT;
    } else if (connection->backend_state
               == BACKEND_CONNECTED) {

        events |= EPOLLRDHUP;

        if (!connection->backend_read_paused) {
            events |= EPOLLIN;
        }

        if (connection->client_bytes_ready > 0) {
            events |= EPOLLOUT;
        }
    }

    return modify_event_source(
        epoll_fd,
        connection->backend_fd,
        events,
        &connection->backend_event_source
    );
}



static void pause_client_read(
    connection_t *connection
)
{
    if (connection->client_read_paused
        || connection->client_read_eof) {

        return;
    }

    connection->client_read_paused = 1;
    connection->client_pause_count++;

    printf(
        "Connection %llu client read paused: "
        "buffered=%zu ready=%zu\n",
        (unsigned long long)connection->id,
        io_buffer_size(
            &connection->client_to_backend
        ),
        connection->client_bytes_ready
    );
}

static void maybe_resume_client_read(
    connection_t *connection
)
{
    if (!connection->client_read_paused
        || connection->client_read_eof) {

        return;
    }

    if (io_buffer_size(
            &connection->client_to_backend)
        > BUFFER_LOW_WATERMARK) {

        return;
    }

    connection->client_read_paused = 0;
    connection->client_resume_count++;

    printf(
        "Connection %llu client read resumed: "
        "buffered=%zu\n",
        (unsigned long long)connection->id,
        io_buffer_size(
            &connection->client_to_backend
        )
    );
}

static void pause_backend_read(
    connection_t *connection
)
{
    if (connection->backend_read_paused
        || connection->backend_read_eof
        || connection->backend_state
           != BACKEND_CONNECTED) {

        return;
    }

    connection->backend_read_paused = 1;
    connection->backend_pause_count++;

    printf(
        "Connection %llu backend read paused: "
        "buffered=%zu\n",
        (unsigned long long)connection->id,
        io_buffer_size(
            &connection->backend_to_client
        )
    );
}

static void maybe_resume_backend_read(
    connection_t *connection
)
{
    if (!connection->backend_read_paused
        || connection->backend_read_eof
        || connection->backend_state
           != BACKEND_CONNECTED) {

        return;
    }

    if (io_buffer_size(
            &connection->backend_to_client)
        > BUFFER_LOW_WATERMARK) {

        return;
    }

    connection->backend_read_paused = 0;
    connection->backend_resume_count++;

    printf(
        "Connection %llu backend read resumed: "
        "buffered=%zu\n",
        (unsigned long long)connection->id,
        io_buffer_size(
            &connection->backend_to_client
        )
    );
}

static int flush_backend_to_client(
    int epoll_fd,
    connection_t *connection
)
{
    while (io_buffer_size(
               &connection->backend_to_client) > 0) {

        ssize_t sent = send(
            connection->fd,
            io_buffer_read_ptr(
                &connection->backend_to_client
            ),
            io_buffer_size(
                &connection->backend_to_client
            ),
            MSG_NOSIGNAL
        );

        if (sent > 0) {
            io_buffer_consume(
                &connection->backend_to_client,
                (size_t)sent
            );

            connection->bytes_forwarded_to_client +=
                (size_t)sent;

            note_activity(connection);

            continue;
        }

        if (sent == -1 && errno == EINTR) {
            continue;
        }

        if (sent == -1
            && (errno == EAGAIN
                || errno == EWOULDBLOCK)) {

            break;
        }

        perror("send client");
        connection_destroy(epoll_fd, connection);
        return -1;
    }

    maybe_resume_backend_read(connection);

    if (!connection->destroyed
        && update_client_interest(
               epoll_fd,
               connection) == -1) {

        perror("epoll_ctl MOD client");
        connection_destroy(epoll_fd, connection);
        return -1;
    }

    if (!connection->destroyed
        && connection->backend_fd != -1
        && update_backend_interest(
               epoll_fd,
               connection) == -1) {

        perror("epoll_ctl MOD backend");
        connection_destroy(epoll_fd, connection);
        return -1;
    }

    return 0;
}


static int retry_backend_before_send(
    int epoll_fd,
    connection_t *connection,
    const char *reason
);

static int flush_client_to_backend(
    int epoll_fd,
    connection_t *connection
)
{
    if (connection->backend_state
        != BACKEND_CONNECTED) {

        return 0;
    }

    while (connection->client_bytes_ready > 0) {
        ssize_t sent = send(
            connection->backend_fd,
            io_buffer_read_ptr(
                &connection->client_to_backend
            ),
            connection->client_bytes_ready,
            MSG_NOSIGNAL
        );

        if (sent > 0) {
            io_buffer_consume(
                &connection->client_to_backend,
                (size_t)sent
            );

            connection->client_bytes_ready -=
                (size_t)sent;

            if (connection->bytes_forwarded_to_backend == 0
                && connection->backend_bytes_received == 0
                && connection->response_wait_started_at_ms == 0) {

                connection->response_wait_started_at_ms =
                    monotonic_ms();
            }

            connection->bytes_forwarded_to_backend +=
                (size_t)sent;

            note_activity(connection);

            continue;
        }

        if (sent == -1 && errno == EINTR) {
            continue;
        }

        if (sent == -1
            && (errno == EAGAIN
                || errno == EWOULDBLOCK)) {

            break;
        }

        perror("send backend");

        if (connection->bytes_forwarded_to_backend == 0
            && retry_backend_before_send(
                   epoll_fd,
                   connection,
                   "backend send failure") == 0) {

            return 0;
        }

        connection->backend_state =
            BACKEND_FAILED;
        connection_destroy(epoll_fd, connection);
        return -1;
    }

    maybe_resume_client_read(connection);

    if (!connection->destroyed
        && update_backend_interest(
               epoll_fd,
               connection) == -1) {

        perror("epoll_ctl MOD backend");
        connection_destroy(epoll_fd, connection);
        return -1;
    }

    if (!connection->destroyed
        && update_client_interest(
               epoll_fd,
               connection) == -1) {

        perror("epoll_ctl MOD client");
        connection_destroy(epoll_fd, connection);
        return -1;
    }

    return 0;
}


static int start_backend_connection(
    int epoll_fd,
    connection_t *connection,
    size_t excluded_index,
    int initial_admission
)
{
    size_t backend_index = 0;

    const backend_target_t *target =
        backend_pool_acquire_next_excluding(
            &backend_pool,
            excluded_index,
            &backend_index
        );

    if (target == NULL) {
        if (initial_admission) {
            total_admission_rejections++;

            printf(
                "Connection %llu admission rejected: "
                "all backends at capacity\n",
                (unsigned long long)connection->id
            );

            connection->backend_state =
                BACKEND_REJECTED;
        } else {
            printf(
                "Connection %llu backend retry unavailable: "
                "no alternate backend has capacity\n",
                (unsigned long long)connection->id
            );

            connection->backend_state =
                BACKEND_FAILED;
        }

        return -1;
    }

    connection->backend_index = backend_index;
    connection->backend_selected = 1;
    connection->backend_slot_acquired = 1;

    printf(
        "Connection %llu selected backend %s "
        "%s:%u (index=%zu active=%zu/%zu)\n",
        (unsigned long long)connection->id,
        target->name,
        target->ip,
        (unsigned int)target->port,
        backend_index,
        target->active_connections,
        target->max_connections
    );

    int backend_fd = -1;

    nonblock_connect_result_t result =
        start_nonblocking_connect_ipv4(
            target->ip,
            target->port,
            &backend_fd
        );

    if (result == NONBLOCK_CONNECT_ERROR) {
        printf(
            "Connection %llu backend %s connect "
            "start failed: %s\n",
            (unsigned long long)connection->id,
            target->name,
            strerror(errno)
        );

        connection->backend_state =
            BACKEND_FAILED;
        release_backend_slot(connection);
        connection->backend_selected = 0;
        return 1;
    }

    connection->backend_fd = backend_fd;

    if (result == NONBLOCK_CONNECT_CONNECTED) {
        connection->backend_state =
            BACKEND_CONNECTED;
        connection->backend_connect_started_at_ms = 0;
        note_activity(connection);

        uint32_t events =
            EPOLLIN
            | EPOLLRDHUP
            | EPOLLERR
            | EPOLLHUP;

        if (connection->client_bytes_ready > 0) {
            events |= EPOLLOUT;
        }

        if (add_event_source(
                epoll_fd,
                backend_fd,
                events,
                &connection->backend_event_source)
            == -1) {

            perror("epoll_ctl ADD backend");
            close_backend(epoll_fd, connection);
            connection->backend_state =
                BACKEND_FAILED;
            return -1;
        }

        printf(
            "Connection %llu backend %s connected "
            "immediately on FD %d\n",
            (unsigned long long)connection->id,
            target->name,
            backend_fd
        );

        return flush_client_to_backend(
            epoll_fd,
            connection
        );
    }

    connection->backend_state =
        BACKEND_CONNECTING;
    connection->backend_connect_started_at_ms =
        monotonic_ms();

    if (add_event_source(
            epoll_fd,
            backend_fd,
            EPOLLOUT | EPOLLERR | EPOLLHUP,
            &connection->backend_event_source)
        == -1) {

        perror("epoll_ctl ADD backend");
        close_backend(epoll_fd, connection);
        connection->backend_state =
            BACKEND_FAILED;
        return -1;
    }

    printf(
        "Connection %llu backend %s connect "
        "in progress on FD %d\n",
        (unsigned long long)connection->id,
        target->name,
        backend_fd
    );

    return 0;
}

static int retry_backend_before_send(
    int epoll_fd,
    connection_t *connection,
    const char *reason
)
{
    if (connection->bytes_forwarded_to_backend != 0
        || connection->backend_bytes_received != 0) {

        total_retry_suppressions++;

        printf(
            "Connection %llu backend retry suppressed after "
            "%zu request bytes were forwarded (%s)\n",
            (unsigned long long)connection->id,
            connection->bytes_forwarded_to_backend,
            reason
        );

        return -1;
    }

    if (connection->backend_retry_count
        >= max_backend_retries) {

        printf(
            "Connection %llu backend retry limit reached "
            "after %zu retries (%s)\n",
            (unsigned long long)connection->id,
            connection->backend_retry_count,
            reason
        );

        return -1;
    }

    size_t failed_index = SIZE_MAX;
    const char *failed_name = "unknown";

    if (connection->backend_selected) {
        failed_index = connection->backend_index;

        const backend_target_t *failed =
            backend_pool_get(
                &backend_pool,
                failed_index
            );

        if (failed != NULL) {
            failed_name = failed->name;
        }
    }

    close_backend(epoll_fd, connection);
    release_backend_slot(connection);

    connection->backend_selected = 0;
    connection->backend_state =
        BACKEND_NOT_STARTED;
    connection->backend_connect_started_at_ms = 0;
    connection->response_wait_started_at_ms = 0;

    connection->backend_retry_count++;
    total_backend_retries++;

    printf(
        "Connection %llu retrying backend after %s: "
        "failed=%s attempt=%zu/%zu\n",
        (unsigned long long)connection->id,
        reason,
        failed_name,
        connection->backend_retry_count,
        max_backend_retries
    );

    int result = start_backend_connection(
        epoll_fd,
        connection,
        failed_index,
        0
    );

    if (result == 1) {
        return -1;
    }

    return result;
}

static int ensure_backend_connection(
    int epoll_fd,
    connection_t *connection
)
{
    if (connection->backend_state
        != BACKEND_NOT_STARTED) {

        return 0;
    }

    int result = start_backend_connection(
        epoll_fd,
        connection,
        SIZE_MAX,
        1
    );

    if (result == 1) {
        return retry_backend_before_send(
            epoll_fd,
            connection,
            "connect start failure"
        );
    }

    return result;
}

static int process_http_frames(
    int epoll_fd,
    connection_t *connection
)
{
    for (;;) {
        size_t buffered = io_buffer_size(
            &connection->client_to_backend
        );

        if (connection->client_bytes_ready
            >= buffered) {

            return 0;
        }

        const unsigned char *base =
            io_buffer_read_ptr(
                &connection->client_to_backend
            );

        size_t unframed =
            buffered
            - connection->client_bytes_ready;

        http_request_frame_t frame;

        http_frame_result_t result =
            http_request_frame_inspect(
                base
                    + connection
                        ->client_bytes_ready,
                unframed,
                &frame
            );

        if (result == HTTP_FRAME_INCOMPLETE) {
            if (unframed > 0
                && connection->request_started_at_ms == 0) {

                connection->request_started_at_ms =
                    monotonic_ms();
            }

            return 0;
        }

        if (result == HTTP_FRAME_ERROR) {
            fprintf(
                stderr,
                "Connection %llu invalid "
                "HTTP framing\n",
                (unsigned long long)
                    connection->id
            );

            return -1;
        }

        connection->client_bytes_ready +=
            frame.total_bytes;
        connection->request_started_at_ms = 0;

        connection->requests_received++;

        printf(
            "Connection %llu HTTP request %zu "
            "complete: header=%zu body=%zu "
            "total=%zu\n",
            (unsigned long long)connection->id,
            connection->requests_received,
            frame.header_bytes,
            frame.content_length,
            frame.total_bytes
        );

        if (connection->backend_state
            == BACKEND_NOT_STARTED) {

            if (ensure_backend_connection(
                    epoll_fd,
                    connection) == -1) {

                return -1;
            }
        }
    }
}

static int handle_client_read(
    int epoll_fd,
    connection_t *connection
)
{
    for (;;) {
        if (connection->backend_state
            == BACKEND_CONNECTED
            && connection->client_bytes_ready > 0) {

            if (flush_client_to_backend(
                    epoll_fd,
                    connection) == -1) {

                return 1;
            }
        }

        size_t buffered = io_buffer_size(
            &connection->client_to_backend
        );

        if (buffered >= BUFFER_HIGH_WATERMARK
            && connection->client_bytes_ready > 0) {

            pause_client_read(connection);

            if (update_client_interest(
                    epoll_fd,
                    connection) == -1) {

                perror("epoll_ctl MOD client");
                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            return 0;
        }

        if (io_buffer_tail_space(
                &connection->client_to_backend)
            < READ_CHUNK) {

            io_buffer_compact(
                &connection->client_to_backend
            );
        }

        size_t available = io_buffer_tail_space(
            &connection->client_to_backend
        );

        if (available > READ_CHUNK) {
            available = READ_CHUNK;
        }

        if (available == 0) {
            if (connection->client_bytes_ready > 0) {
                pause_client_read(connection);

                if (update_client_interest(
                        epoll_fd,
                        connection) == -1) {

                    perror("epoll_ctl MOD client");
                    connection_destroy(
                        epoll_fd,
                        connection
                    );
                    return 1;
                }

                return 0;
            }

            fprintf(
                stderr,
                "Connection %llu request exceeds "
                "client framing buffer\n",
                (unsigned long long)connection->id
            );

            connection_destroy(epoll_fd, connection);
            return 1;
        }

        ssize_t received = recv(
            connection->fd,
            io_buffer_write_ptr(
                &connection->client_to_backend
            ),
            available,
            0
        );

        if (received > 0) {
            io_buffer_produce(
                &connection->client_to_backend,
                (size_t)received
            );

            connection->bytes_received +=
                (size_t)received;
            note_activity(connection);

            if (process_http_frames(
                    epoll_fd,
                    connection) == -1) {

                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            if (connection->backend_state
                == BACKEND_CONNECTED
                && connection->client_bytes_ready > 0) {

                if (flush_client_to_backend(
                        epoll_fd,
                        connection) == -1) {

                    return 1;
                }
            }

            buffered = io_buffer_size(
                &connection->client_to_backend
            );

            if (buffered >= BUFFER_HIGH_WATERMARK
                && connection->client_bytes_ready > 0) {

                pause_client_read(connection);

                if (update_client_interest(
                        epoll_fd,
                        connection) == -1) {

                    perror("epoll_ctl MOD client");
                    connection_destroy(
                        epoll_fd,
                        connection
                    );
                    return 1;
                }

                return 0;
            }

            continue;
        }

        if (received == 0) {
            connection->client_read_eof = 1;

            printf(
                "Connection %llu client reached EOF\n",
                (unsigned long long)connection->id
            );

            if (io_buffer_size(
                    &connection->client_to_backend)
                > connection->client_bytes_ready) {

                printf(
                    "Connection %llu reached EOF with "
                    "%zu unframed client bytes\n",
                    (unsigned long long)connection->id,
                    io_buffer_size(
                        &connection->client_to_backend
                    ) - connection->client_bytes_ready
                );

                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            if (connection->backend_state == BACKEND_NOT_STARTED
                && connection->client_bytes_ready == 0
                && io_buffer_size(
                       &connection->backend_to_client) == 0) {

                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            if (update_client_interest(
                    epoll_fd,
                    connection) == -1) {

                perror("epoll_ctl MOD client");
                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN
            || errno == EWOULDBLOCK) {

            return 0;
        }

        perror("recv client");
        connection_destroy(epoll_fd, connection);
        return 1;
    }
}


static int handle_backend_read(
    int epoll_fd,
    connection_t *connection
)
{
    for (;;) {
        if (io_buffer_size(
                &connection->backend_to_client) > 0) {

            if (flush_backend_to_client(
                    epoll_fd,
                    connection) == -1) {

                return 1;
            }
        }

        if (connection->destroyed) {
            return 1;
        }

        size_t buffered = io_buffer_size(
            &connection->backend_to_client
        );

        if (buffered >= BUFFER_HIGH_WATERMARK) {
            pause_backend_read(connection);

            if (update_backend_interest(
                    epoll_fd,
                    connection) == -1) {

                perror("epoll_ctl MOD backend");
                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            return 0;
        }

        if (io_buffer_tail_space(
                &connection->backend_to_client)
            < READ_CHUNK) {

            io_buffer_compact(
                &connection->backend_to_client
            );
        }

        size_t available = io_buffer_tail_space(
            &connection->backend_to_client
        );

        if (available > READ_CHUNK) {
            available = READ_CHUNK;
        }

        if (available == 0) {
            pause_backend_read(connection);

            if (update_backend_interest(
                    epoll_fd,
                    connection) == -1) {

                perror("epoll_ctl MOD backend");
                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            return 0;
        }

        ssize_t received = recv(
            connection->backend_fd,
            io_buffer_write_ptr(
                &connection->backend_to_client
            ),
            available,
            0
        );

        if (received > 0) {
            io_buffer_produce(
                &connection->backend_to_client,
                (size_t)received
            );

            connection->backend_bytes_received +=
                (size_t)received;
            connection->response_wait_started_at_ms = 0;
            note_activity(connection);

            if (flush_backend_to_client(
                    epoll_fd,
                    connection) == -1) {

                return 1;
            }

            if (connection->destroyed) {
                return 1;
            }

            if (io_buffer_size(
                    &connection->backend_to_client)
                >= BUFFER_HIGH_WATERMARK) {

                pause_backend_read(connection);

                if (update_backend_interest(
                        epoll_fd,
                        connection) == -1) {

                    perror("epoll_ctl MOD backend");
                    connection_destroy(
                        epoll_fd,
                        connection
                    );
                    return 1;
                }

                return 0;
            }

            continue;
        }

        if (received == 0) {
            connection->backend_read_eof = 1;
            connection->backend_state =
                BACKEND_CLOSED;

            printf(
                "Connection %llu backend reached EOF\n",
                (unsigned long long)connection->id
            );

            if (connection->backend_bytes_received == 0
                && connection->bytes_forwarded_to_backend > 0) {

                total_retry_suppressions++;
                printf(
                    "Connection %llu backend retry suppressed after "
                    "%zu request bytes were forwarded "
                    "(backend EOF before response)\n",
                    (unsigned long long)connection->id,
                    connection->bytes_forwarded_to_backend
                );
            }

            close_backend(epoll_fd, connection);

            if (io_buffer_size(
                    &connection->backend_to_client)
                == 0) {

                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            if (update_client_interest(
                    epoll_fd,
                    connection) == -1) {

                perror("epoll_ctl MOD client");
                connection_destroy(
                    epoll_fd,
                    connection
                );
                return 1;
            }

            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN
            || errno == EWOULDBLOCK) {

            return 0;
        }

        perror("recv backend");
        connection->backend_state =
            BACKEND_FAILED;
        connection_destroy(epoll_fd, connection);
        return 1;
    }
}


static void handle_backend_event(
    int epoll_fd,
    connection_t *connection,
    uint32_t events
)
{
    if (connection == NULL
        || connection->destroyed) {

        return;
    }

    const backend_target_t *target = NULL;

    if (connection->backend_selected) {
        target = backend_pool_get(
            &backend_pool,
            connection->backend_index
        );
    }

    if (connection->backend_state
        == BACKEND_CONNECTING) {

        if ((events
             & (EPOLLOUT | EPOLLERR | EPOLLHUP))
            == 0U) {

            return;
        }

        if (finish_nonblocking_connect(
                connection->backend_fd) == -1) {

            printf(
                "Connection %llu backend %s connect "
                "failed: %s\n",
                (unsigned long long)connection->id,
                target != NULL ? target->name : "?",
                strerror(errno)
            );

            connection->backend_state =
                BACKEND_FAILED;

            if (retry_backend_before_send(
                    epoll_fd,
                    connection,
                    "backend connect failure") == 0) {

                return;
            }

            connection_destroy(epoll_fd, connection);
            return;
        }

        connection->backend_state =
            BACKEND_CONNECTED;
        connection->backend_connect_started_at_ms = 0;
        note_activity(connection);

        printf(
            "Connection %llu backend %s connected "
            "on FD %d\n",
            (unsigned long long)connection->id,
            target != NULL ? target->name : "?",
            connection->backend_fd
        );

        if (update_backend_interest(
                epoll_fd,
                connection) == -1) {

            perror("epoll_ctl MOD backend");
            connection_destroy(epoll_fd, connection);
            return;
        }

        if (flush_client_to_backend(
                epoll_fd,
                connection) == -1) {

            return;
        }
    }

    if (connection->destroyed
        || connection->backend_state
           != BACKEND_CONNECTED) {

        return;
    }

    if ((events & EPOLLOUT) != 0U
        && connection->client_bytes_ready > 0) {

        if (flush_client_to_backend(
                epoll_fd,
                connection) == -1) {

            return;
        }
    }

    if (connection->destroyed) {
        return;
    }

    if ((events
         & (EPOLLIN | EPOLLRDHUP | EPOLLHUP))
        != 0U
        && !connection->backend_read_paused) {

        if (handle_backend_read(
                epoll_fd,
                connection)) {

            return;
        }
    }

    if (connection->destroyed) {
        return;
    }

    if ((events & EPOLLERR) != 0U) {
        int socket_error = 0;
        socklen_t length = sizeof(socket_error);

        if (getsockopt(
                connection->backend_fd,
                SOL_SOCKET,
                SO_ERROR,
                &socket_error,
                &length) == -1) {

            perror("getsockopt backend SO_ERROR");
        } else if (socket_error != 0) {
            fprintf(
                stderr,
                "Connection %llu backend socket "
                "error: %s\n",
                (unsigned long long)connection->id,
                strerror(socket_error)
            );
        }

        connection->backend_state =
            BACKEND_FAILED;
        connection_destroy(epoll_fd, connection);
    }
}


static void handle_client_event(
    int epoll_fd,
    connection_t *connection,
    uint32_t events
)
{
    if (connection == NULL
        || connection->destroyed) {

        return;
    }

    if ((events & EPOLLOUT) != 0U
        && io_buffer_size(
               &connection->backend_to_client) > 0) {

        if (flush_backend_to_client(
                epoll_fd,
                connection) == -1) {

            return;
        }
    }

    if (connection->destroyed) {
        return;
    }

    if ((events & (EPOLLIN | EPOLLRDHUP)) != 0U
        && !connection->client_read_eof
        && !connection->client_read_paused) {

        if (handle_client_read(
                epoll_fd,
                connection)) {

            return;
        }
    }

    if (connection->destroyed) {
        return;
    }

    if ((events & (EPOLLERR | EPOLLHUP)) != 0U) {
        printf(
            "Connection %llu client hangup/error\n",
            (unsigned long long)connection->id
        );

        connection_destroy(epoll_fd, connection);
        return;
    }

    if (connection->backend_read_eof
        && io_buffer_size(
               &connection->backend_to_client) == 0) {

        connection_destroy(epoll_fd, connection);
    }
}


static int timer_remaining_ms(
    uint64_t now_ms,
    uint64_t started_ms,
    uint64_t timeout_ms
)
{
    if (started_ms == 0) {
        return INT_MAX;
    }

    uint64_t elapsed = elapsed_ms(
        now_ms,
        started_ms
    );

    if (elapsed >= timeout_ms) {
        return 0;
    }

    uint64_t remaining = timeout_ms - elapsed;

    if (remaining > (uint64_t)INT_MAX) {
        return INT_MAX;
    }

    return (int)remaining;
}

static int connection_wait_timeout_ms(
    const connection_t *connection,
    uint64_t now_ms
)
{
    int nearest = timer_remaining_ms(
        now_ms,
        connection->last_activity_ms,
        timeout_config.idle_ms
    );

    int remaining = timer_remaining_ms(
        now_ms,
        connection->request_started_at_ms,
        timeout_config.request_ms
    );

    if (remaining < nearest) {
        nearest = remaining;
    }

    if (connection->backend_state == BACKEND_CONNECTING) {
        remaining = timer_remaining_ms(
            now_ms,
            connection->backend_connect_started_at_ms,
            timeout_config.connect_ms
        );

        if (remaining < nearest) {
            nearest = remaining;
        }
    }

    remaining = timer_remaining_ms(
        now_ms,
        connection->response_wait_started_at_ms,
        timeout_config.response_ms
    );

    if (remaining < nearest) {
        nearest = remaining;
    }

    return nearest;
}

static int compute_epoll_wait_timeout_ms(
    uint64_t now_ms
)
{
    if (connections == NULL) {
        return -1;
    }

    int nearest = INT_MAX;

    for (connection_t *connection = connections;
         connection != NULL;
         connection = connection->next) {

        if (connection->destroyed) {
            continue;
        }

        int remaining = connection_wait_timeout_ms(
            connection,
            now_ms
        );

        if (remaining < nearest) {
            nearest = remaining;
        }
    }

    if (nearest == INT_MAX) {
        return -1;
    }

    return nearest;
}

static void timeout_connection(
    int epoll_fd,
    connection_t *connection,
    const char *kind,
    uint64_t elapsed
)
{
    printf(
        "Connection %llu %s timeout after %llu ms\n",
        (unsigned long long)connection->id,
        kind,
        (unsigned long long)elapsed
    );

    total_timeouts++;
    connection_destroy(epoll_fd, connection);
}

static void check_connection_timeouts(
    int epoll_fd,
    uint64_t now_ms
)
{
    connection_t *connection = connections;

    while (connection != NULL) {
        connection_t *next = connection->next;

        if (connection->destroyed) {
            connection = next;
            continue;
        }

        if (connection->backend_state == BACKEND_CONNECTING
            && connection->backend_connect_started_at_ms != 0
            && elapsed_ms(
                   now_ms,
                   connection->backend_connect_started_at_ms)
               >= timeout_config.connect_ms) {

            uint64_t connect_elapsed = elapsed_ms(
                now_ms,
                connection->backend_connect_started_at_ms
            );

            printf(
                "Connection %llu backend connect timeout "
                "after %llu ms\n",
                (unsigned long long)connection->id,
                (unsigned long long)connect_elapsed
            );
            total_timeouts++;
            connection->backend_state = BACKEND_FAILED;

            if (retry_backend_before_send(
                    epoll_fd,
                    connection,
                    "backend connect timeout") == -1) {

                connection_destroy(epoll_fd, connection);
            }

            connection = next;
            continue;
        }

        if (connection->request_started_at_ms != 0
            && elapsed_ms(
                   now_ms,
                   connection->request_started_at_ms)
               >= timeout_config.request_ms) {

            timeout_connection(
                epoll_fd,
                connection,
                "HTTP request",
                elapsed_ms(
                    now_ms,
                    connection->request_started_at_ms
                )
            );

            connection = next;
            continue;
        }

        if (connection->response_wait_started_at_ms != 0
            && elapsed_ms(
                   now_ms,
                   connection->response_wait_started_at_ms)
               >= timeout_config.response_ms) {

            timeout_connection(
                epoll_fd,
                connection,
                "backend response",
                elapsed_ms(
                    now_ms,
                    connection->response_wait_started_at_ms
                )
            );

            connection = next;
            continue;
        }

        if (connection->last_activity_ms != 0
            && elapsed_ms(
                   now_ms,
                   connection->last_activity_ms)
               >= timeout_config.idle_ms) {

            timeout_connection(
                epoll_fd,
                connection,
                "idle",
                elapsed_ms(
                    now_ms,
                    connection->last_activity_ms
                )
            );
        }

        connection = next;
    }
}

static int accept_ready_clients(
    int epoll_fd,
    int listener_fd
)
{
    for (;;) {
        struct sockaddr_in client_address;
        socklen_t client_length =
            sizeof(client_address);

        int client_fd = accept(
            listener_fd,
            (struct sockaddr *)&client_address,
            &client_length
        );

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN
                || errno == EWOULDBLOCK) {

                return 0;
            }

            perror("accept");
            return -1;
        }

        if (set_nonblocking(client_fd) == -1) {
            perror("set_nonblocking client");
            close(client_fd);
            continue;
        }

        uint64_t id = next_connection_id++;

        connection_t *connection =
            connection_create(client_fd, id);

        if (connection == NULL) {
            perror("connection_create");
            close(client_fd);
            continue;
        }

        uint64_t accepted_ms = monotonic_ms();
        connection->accepted_at_ms = accepted_ms;
        connection->last_activity_ms = accepted_ms;

        connection_list_add(connection);

        if (add_event_source(
                epoll_fd,
                client_fd,
                EPOLLIN
                    | EPOLLRDHUP
                    | EPOLLERR
                    | EPOLLHUP,
                &connection->event_source)
            == -1) {

            perror("epoll_ctl ADD client");
            connection_list_remove(connection);
            close(client_fd);
            connection_free(connection);
            continue;
        }

        total_connections++;
        active_connections++;

        if (active_connections > peak_connections) {
            peak_connections = active_connections;
        }

        char client_ip[INET_ADDRSTRLEN];

        if (inet_ntop(
                AF_INET,
                &client_address.sin_addr,
                client_ip,
                sizeof(client_ip)) == NULL) {

            strcpy(client_ip, "?");
        }

        printf(
            "Connection %llu opened on FD %d "
            "from %s:%u\n",
            (unsigned long long)id,
            client_fd,
            client_ip,
            (unsigned int)ntohs(
                client_address.sin_port
            )
        );
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (load_timeout_config() == -1) {
        return EXIT_FAILURE;
    }

    if (parse_backend_capacity_env() == -1) {
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < BACKEND_COUNT; ++i) {
        backend_targets[i].max_connections =
            backend_max_connections;
    }

    if (backend_pool_init(
            &backend_pool,
            backend_targets,
            BACKEND_COUNT) == -1) {

        fprintf(stderr, "backend_pool_init failed\n");
        return EXIT_FAILURE;
    }

    if (install_signal_handlers() == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int listener_fd = create_listener();

    if (listener_fd == -1) {
        perror("create_listener");
        return EXIT_FAILURE;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listener_fd);
        return EXIT_FAILURE;
    }

    event_source_t listener_source;
    listener_source.kind =
        EVENT_SOURCE_LISTENER;
    listener_source.object = NULL;

    if (add_event_source(
            epoll_fd,
            listener_fd,
            EPOLLIN,
            &listener_source) == -1) {

        perror("epoll_ctl ADD listener");
        close(epoll_fd);
        close(listener_fd);
        return EXIT_FAILURE;
    }

    printf(
        "Micro-proxy event server listening "
        "on 127.0.0.1:%d\n",
        PORT
    );

    printf(
        "Timeouts: connect=%llu ms idle=%llu ms "
        "request=%llu ms response=%llu ms\n",
        (unsigned long long)timeout_config.connect_ms,
        (unsigned long long)timeout_config.idle_ms,
        (unsigned long long)timeout_config.request_ms,
        (unsigned long long)timeout_config.response_ms
    );

    printf("Backends: ");

    for (size_t i = 0; i < BACKEND_COUNT; ++i) {
        printf(
            "%s%s=%s:%u(max=%zu)",
            i == 0 ? "" : ", ",
            backend_targets[i].name,
            backend_targets[i].ip,
            (unsigned int)backend_targets[i].port,
            backend_targets[i].max_connections
        );
    }

    printf("\n");

    struct epoll_event events[MAX_EVENTS];

    while (!stop_requested) {
        uint64_t before_wait_ms = monotonic_ms();

        check_connection_timeouts(
            epoll_fd,
            before_wait_ms
        );
        free_destroyed_connections();

        int wait_timeout_ms =
            compute_epoll_wait_timeout_ms(
                monotonic_ms()
            );

        int ready = epoll_wait(
            epoll_fd,
            events,
            MAX_EVENTS,
            wait_timeout_ms
        );

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait");
            break;
        }

        if (ready == 0) {
            continue;
        }

        for (int i = 0; i < ready; ++i) {
            event_source_t *source =
                events[i].data.ptr;

            if (source == NULL) {
                continue;
            }

            if (source->kind
                == EVENT_SOURCE_LISTENER) {

                if (accept_ready_clients(
                        epoll_fd,
                        listener_fd) == -1) {

                    stop_requested = 1;
                    break;
                }

                continue;
            }

            if (source->kind
                == EVENT_SOURCE_CLIENT) {

                connection_t *connection =
                    source->object;

                if (connection == NULL
                    || connection->destroyed) {

                    continue;
                }

                handle_client_event(
                    epoll_fd,
                    connection,
                    events[i].events
                );

                continue;
            }

            if (source->kind
                == EVENT_SOURCE_BACKEND) {

                connection_t *connection =
                    source->object;

                if (connection == NULL
                    || connection->destroyed) {

                    continue;
                }

                handle_backend_event(
                    epoll_fd,
                    connection,
                    events[i].events
                );
            }
        }

        free_destroyed_connections();
    }

    destroy_all_connections(epoll_fd);
    free_destroyed_connections();

    printf(
        "Proxy summary: total_connections=%zu "
        "peak_connections=%zu "
        "active_connections=%zu "
        "total_client_bytes=%zu "
        "timeouts=%zu "
        "admission_rejections=%zu "
        "backend_retries=%zu "
        "retry_suppressions=%zu\n",
        total_connections,
        peak_connections,
        active_connections,
        total_bytes_received,
        total_timeouts,
        total_admission_rejections,
        total_backend_retries,
        total_retry_suppressions
    );

    if (close(epoll_fd) == -1) {
        perror("close epoll");
    }

    if (close(listener_fd) == -1) {
        perror("close listener");
        return EXIT_FAILURE;
    }

    printf(
        "Micro-proxy event server shut down cleanly\n"
    );

    return EXIT_SUCCESS;
}
