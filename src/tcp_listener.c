#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define BACKLOG 16
#define BUFFER_SIZE 1024

int main(void)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

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
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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

    printf("TCP listener PID: %ld\n", (long)getpid());
    printf("Listening FD: %d\n", server_fd);
    printf("Listening on 127.0.0.1:%d\n", PORT);
    fflush(stdout);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int client_fd = accept(
        server_fd,
        (struct sockaddr *)&client_addr,
        &client_addr_len
    );

    if (client_fd == -1) {
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
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf(
        "Accepted client %s:%u on FD %d\n",
        client_ip,
        (unsigned int)ntohs(client_addr.sin_port),
        client_fd
    );

    char buffer[BUFFER_SIZE];

    ssize_t bytes_received = recv(
        client_fd,
        buffer,
        sizeof(buffer) - 1,
        0
    );

    if (bytes_received == -1) {
        perror("recv");
        close(client_fd);
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (bytes_received == 0) {
        printf("Client closed connection without sending data\n");
    } else {
        buffer[bytes_received] = '\0';

        printf(
            "Received %zd bytes: %s",
            bytes_received,
            buffer
        );

        const char response[] =
            "hello from phase03 tcp listener\n";

        ssize_t bytes_sent = send(
            client_fd,
            response,
            sizeof(response) - 1,
            0
        );

        if (bytes_sent == -1) {
            perror("send");
            close(client_fd);
            close(server_fd);
            return EXIT_FAILURE;
        }

        printf("Sent %zd bytes\n", bytes_sent);
    }

    close(client_fd);
    close(server_fd);

    return EXIT_SUCCESS;
}
