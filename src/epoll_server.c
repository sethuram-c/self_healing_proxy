#define _POSIX_C_SOURCE 200809L

#include "connection.h"
#include "http_framing.h"
#include "net_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8081
#define BACKLOG 128
#define MAX_EVENTS 64
#define READ_CHUNK 1024
#define BACKEND_IP "127.0.0.1"
#define BACKEND_PORT 9001

static volatile sig_atomic_t stop_requested = 0;

static uint64_t next_connection_id = 1;

static connection_t *connections = NULL;
static connection_t *garbage_connections = NULL;

static size_t total_connections = 0;
static size_t active_connections = 0;
static size_t peak_connections = 0;
static size_t total_bytes_received = 0;

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
    connection_list_remove(connection);

    if (close(fd) == -1) {
        perror("close client");
    }

    queue_connection_for_free(connection);

    printf(
        "Connection %llu FD %d closed after "
        "%zu client bytes, %zu HTTP requests, "
        "%zu bytes to backend, %zu bytes to client\n",
        (unsigned long long)id,
        fd,
        bytes,
        requests,
        to_backend,
        from_backend
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

    if (!connection->client_read_eof) {
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

        events |= EPOLLIN | EPOLLRDHUP;

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

            connection->bytes_forwarded_to_backend +=
                (size_t)sent;

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
        connection->backend_state =
            BACKEND_FAILED;
        connection_destroy(epoll_fd, connection);
        return -1;
    }

    if (!connection->destroyed
        && update_backend_interest(
               epoll_fd,
               connection) == -1) {

        perror("epoll_ctl MOD backend");
        connection_destroy(epoll_fd, connection);
        return -1;
    }

    return 0;
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

    int backend_fd = -1;

    nonblock_connect_result_t result =
        start_nonblocking_connect_ipv4(
            BACKEND_IP,
            BACKEND_PORT,
            &backend_fd
        );

    if (result == NONBLOCK_CONNECT_ERROR) {
        printf(
            "Connection %llu backend connect "
            "start failed: %s\n",
            (unsigned long long)connection->id,
            strerror(errno)
        );

        connection->backend_state =
            BACKEND_FAILED;

        return -1;
    }

    connection->backend_fd = backend_fd;

    if (result
        == NONBLOCK_CONNECT_CONNECTED) {

        connection->backend_state =
            BACKEND_CONNECTED;

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
            "Connection %llu backend connected "
            "immediately on FD %d\n",
            (unsigned long long)connection->id,
            backend_fd
        );

        return flush_client_to_backend(
            epoll_fd,
            connection
        );
    }

    connection->backend_state =
        BACKEND_CONNECTING;

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
        "Connection %llu backend connect "
        "in progress on FD %d\n",
        (unsigned long long)connection->id,
        backend_fd
    );

    return 0;
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
            fprintf(
                stderr,
                "Connection %llu client-to-backend "
                "buffer full\n",
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

            printf(
                "Connection %llu client FD %d "
                "read %zd bytes (total %zu)\n",
                (unsigned long long)connection->id,
                connection->fd,
                received,
                connection->bytes_received
            );

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
            fprintf(
                stderr,
                "Connection %llu backend-to-client "
                "buffer full\n",
                (unsigned long long)connection->id
            );

            connection_destroy(epoll_fd, connection);
            return 1;
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

            printf(
                "Connection %llu backend FD %d "
                "read %zd bytes (total %zu)\n",
                (unsigned long long)connection->id,
                connection->backend_fd,
                received,
                connection->backend_bytes_received
            );

            if (flush_backend_to_client(
                    epoll_fd,
                    connection) == -1) {

                return 1;
            }

            continue;
        }

        if (received == 0) {
            connection->backend_read_eof = 1;

            printf(
                "Connection %llu backend reached EOF\n",
                (unsigned long long)connection->id
            );

            close_backend(epoll_fd, connection);
            connection->backend_state =
                BACKEND_CLOSED;

            if (io_buffer_size(
                    &connection->backend_to_client) == 0) {

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
                "Connection %llu backend connect "
                "failed: %s\n",
                (unsigned long long)connection->id,
                strerror(errno)
            );

            connection->backend_state =
                BACKEND_FAILED;
            connection_destroy(epoll_fd, connection);
            return;
        }

        connection->backend_state =
            BACKEND_CONNECTED;

        printf(
            "Connection %llu backend connected "
            "on FD %d\n",
            (unsigned long long)connection->id,
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
        != 0U) {

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
        && !connection->client_read_eof) {

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

    struct epoll_event events[MAX_EVENTS];

    while (!stop_requested) {
        int ready = epoll_wait(
            epoll_fd,
            events,
            MAX_EVENTS,
            -1
        );

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait");
            break;
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
        "total_client_bytes=%zu\n",
        total_connections,
        peak_connections,
        active_connections,
        total_bytes_received
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
