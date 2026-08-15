#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define BACKLOG 16
#define BUFFER_SIZE 1024

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

    /*
     * Deliberately do not use SA_RESTART.
     *
     * We want accept() to return EINTR when SIGINT/SIGTERM
     * arrives so that the main loop can notice stop_requested.
     */
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1) {
        return -1;
    }

    if (sigaction(SIGTERM, &action, NULL) == -1) {
        return -1;
    }

    return 0;
}

static int send_all(int fd, const char *buffer, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(
            fd,
            buffer + total_sent,
            length - total_sent,
            MSG_NOSIGNAL
        );

        if (sent > 0) {
            total_sent += (size_t)sent;
            continue;
        }

        if (sent == -1 && errno == EINTR) {
            continue;
        }

        return -1;
    }

    return 0;
}

static int handle_client(int client_fd)
{
    char buffer[BUFFER_SIZE];

    ssize_t received;

    for (;;) {
        received = recv(
            client_fd,
            buffer,
            sizeof(buffer) - 1,
            0
        );

        if (received == -1 && errno == EINTR) {
            continue;
        }

        break;
    }

    if (received == -1) {
        perror("recv");
        return -1;
    }

    if (received == 0) {
        printf("Client closed without sending data\n");
        return 0;
    }

    buffer[received] = '\0';

    printf(
        "Received %zd bytes: %s",
        received,
        buffer
    );

    const char response[] =
        "hello from phase04 tcp server\n";

    if (send_all(
            client_fd,
            response,
            sizeof(response) - 1) == -1) {
        perror("send");
        return -1;
    }

    return 0;
}

int main(void)
{
    if (install_signal_handlers() == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int enable = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &enable,
            sizeof(enable)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(PORT);

    if (bind(
            server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("TCP server PID: %ld\n", (long)getpid());
    printf("Listening FD: %d\n", server_fd);
    printf("Listening on 127.0.0.1:%d\n", PORT);

    fflush(stdout);

    while (!stop_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len =
            sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &client_addr_len
        );

        if (client_fd == -1) {
            if (errno == EINTR) {
                if (stop_requested) {
                    break;
                }

                continue;
            }

            perror("accept");
            close(server_fd);
            return EXIT_FAILURE;
        }

        char client_ip[INET_ADDRSTRLEN];

        if (inet_ntop(
                AF_INET,
                &client_addr.sin_addr,
                client_ip,
                sizeof(client_ip)) == NULL) {
            perror("inet_ntop");
            close(client_fd);
            continue;
        }

        printf(
            "Accepted %s:%u on FD %d\n",
            client_ip,
            (unsigned int)ntohs(
                client_addr.sin_port),
            client_fd
        );

        (void)handle_client(client_fd);

        if (close(client_fd) == -1) {
            perror("close client");
        }
    }

    if (close(server_fd) == -1) {
        perror("close listener");
        return EXIT_FAILURE;
    }

    printf("TCP server shut down cleanly\n");

    return EXIT_SUCCESS;
}
