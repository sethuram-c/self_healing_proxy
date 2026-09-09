#define _POSIX_C_SOURCE 200809L
#include "uds.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_BACKLOG 16
#define BUFFER_SIZE 4096

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signo) {
    (void)signo;
    stop_requested = 1;
}

static void print_usage(FILE *stream) {
    fprintf(stream,
            "Usage: uds_server --socket PATH\n"
            "Blocking Phase 29 AF_UNIX/SOCK_STREAM lab server.\n"
            "The socket is created mode 0600, peer credentials are inspected,\n"
            "and received bytes are echoed without message framing.\n");
}

static int install_signal_handlers(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        return -1;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return -1;
    }

    return 0;
}

static int send_all(int fd, const unsigned char *data, size_t len) {
    size_t off = 0;

    while (off < len) {
        ssize_t n = send(fd, data + off, len - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }

    return 0;
}

static int serve_client(int fd) {
    unsigned char buffer[BUFFER_SIZE];

    for (;;) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            if (send_all(fd, buffer, (size_t)n) < 0) {
                return -1;
            }
            continue;
        }
        if (n == 0) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
}

int main(int argc, char **argv) {
    const char *socket_path = NULL;
    int listener_fd = -1;
    int rc = EXIT_FAILURE;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--socket") == 0) {
            if (i + 1 >= argc || socket_path != NULL) {
                fprintf(stderr, "--socket requires exactly one PATH\n");
                print_usage(stderr);
                return 2;
            }
            socket_path = argv[++i];
            continue;
        }
        fprintf(stderr, "unknown argument: %s\n", argv[i]);
        print_usage(stderr);
        return 2;
    }

    if (socket_path == NULL) {
        fprintf(stderr, "--socket PATH is required\n");
        print_usage(stderr);
        return 2;
    }

    if (install_signal_handlers() < 0) {
        perror("install signal handlers");
        return EXIT_FAILURE;
    }

    listener_fd = uds_listener_open(socket_path, 0600, DEFAULT_BACKLOG);
    if (listener_fd < 0) {
        fprintf(stderr, "failed to open Unix socket %s: %s\n", socket_path, strerror(errno));
        return EXIT_FAILURE;
    }

    printf("PHASE29_UDS_READY path=%s mode=0600 backlog=%d\n", socket_path, DEFAULT_BACKLOG);
    fflush(stdout);

    rc = EXIT_SUCCESS;
    while (!stop_requested) {
        uds_peer_cred_t peer;
        int client_fd = uds_accept_peer(listener_fd, &peer);

        if (client_fd < 0) {
            if (errno == EINTR && stop_requested) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "accept failed: %s\n", strerror(errno));
            rc = EXIT_FAILURE;
            break;
        }

        printf("PHASE29_PEER pid=%ld uid=%ld gid=%ld\n",
               (long)peer.pid, (long)peer.uid, (long)peer.gid);
        fflush(stdout);

        if (serve_client(client_fd) < 0 && errno != EPIPE && errno != ECONNRESET) {
            fprintf(stderr, "client stream failed: %s\n", strerror(errno));
        }
        close(client_fd);
    }

    close(listener_fd);
    if (uds_cleanup_path(socket_path) < 0) {
        fprintf(stderr, "failed to clean Unix socket %s: %s\n", socket_path, strerror(errno));
        return EXIT_FAILURE;
    }

    printf("PHASE29_UDS_CLEANUP_OK\n");
    fflush(stdout);
    return rc;
}
