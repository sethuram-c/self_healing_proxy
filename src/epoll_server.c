#define _POSIX_C_SOURCE 200809L

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
#define READ_BUFFER_SIZE 4096

static volatile sig_atomic_t stop_requested = 0;

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
    int fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

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

    memset(
        &address,
        0,
        sizeof(address)
    );

    address.sin_family = AF_INET;

    address.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);

    address.sin_port =
        htons(PORT);

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

static int add_to_epoll(
    int epoll_fd,
    int fd,
    uint32_t events
)
{
    struct epoll_event event;

    memset(
        &event,
        0,
        sizeof(event)
    );

    event.events = events;
    event.data.fd = fd;

    return epoll_ctl(
        epoll_fd,
        EPOLL_CTL_ADD,
        fd,
        &event
    );
}

static void close_client(
    int epoll_fd,
    int client_fd
)
{
    if (epoll_ctl(
            epoll_fd,
            EPOLL_CTL_DEL,
            client_fd,
            NULL) == -1
        && errno != ENOENT) {

        perror("epoll_ctl DEL");
    }

    if (close(client_fd) == -1) {
        perror("close client");
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

        if (add_to_epoll(
                epoll_fd,
                client_fd,
                EPOLLIN | EPOLLRDHUP) == -1) {

            perror("epoll_ctl ADD client");
            close(client_fd);
            continue;
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
            "Accepted %s:%u on FD %d\n",
            client_ip,
            (unsigned int)ntohs(
                client_address.sin_port),
            client_fd
        );
    }
}

static void handle_client_event(
    int epoll_fd,
    int client_fd,
    uint32_t events
)
{
    unsigned char buffer[READ_BUFFER_SIZE];

    for (;;) {
        ssize_t received = recv(
            client_fd,
            buffer,
            sizeof(buffer),
            0
        );

        if (received > 0) {
            printf(
                "FD %d read %zd bytes\n",
                client_fd,
                received
            );

            continue;
        }

        if (received == 0) {
            printf(
                "FD %d reached EOF\n",
                client_fd
            );

            close_client(
                epoll_fd,
                client_fd
            );

            return;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN
            || errno == EWOULDBLOCK) {
            break;
        }

        perror("recv");

        close_client(
            epoll_fd,
            client_fd
        );

        return;
    }

    if ((events & (EPOLLERR | EPOLLHUP)) != 0U) {
        printf(
            "FD %d hangup/error event\n",
            client_fd
        );

        close_client(
            epoll_fd,
            client_fd
        );
    }
}

int main(void)
{
    setvbuf(
        stdout,
        NULL,
        _IOLBF,
        0
    );

    if (install_signal_handlers() == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int listener_fd = create_listener();

    if (listener_fd == -1) {
        perror("create_listener");
        return EXIT_FAILURE;
    }

    int epoll_fd =
        epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listener_fd);

        return EXIT_FAILURE;
    }

    if (add_to_epoll(
            epoll_fd,
            listener_fd,
            EPOLLIN) == -1) {

        perror("epoll_ctl ADD listener");

        close(epoll_fd);
        close(listener_fd);

        return EXIT_FAILURE;
    }

    printf(
        "epoll server listening on "
        "127.0.0.1:%d\n",
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
            int fd =
                events[i].data.fd;

            if (fd == listener_fd) {
                if (accept_ready_clients(
                        epoll_fd,
                        listener_fd) == -1) {

                    stop_requested = 1;
                    break;
                }

                continue;
            }

            handle_client_event(
                epoll_fd,
                fd,
                events[i].events
            );
        }
    }

    if (close(epoll_fd) == -1) {
        perror("close epoll");
    }

    if (close(listener_fd) == -1) {
        perror("close listener");

        return EXIT_FAILURE;
    }

    printf(
        "epoll server shut down cleanly\n"
    );

    return EXIT_SUCCESS;
}
