#define _POSIX_C_SOURCE 200809L
#include "ipc_frame.h"
#include "uds.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signo)
{
    (void)signo;
    g_stop = 1;
}

typedef struct echo_ctx {
    int fd;
    unsigned long frames;
} echo_ctx_t;

static int echo_frame(uint8_t type,
                      const uint8_t *payload,
                      uint32_t payload_len,
                      void *opaque)
{
    echo_ctx_t *ctx = opaque;

    ctx->frames++;
    printf("PHASE30_FRAME type=%u len=%u seq=%lu\n",
           (unsigned)type,
           payload_len,
           ctx->frames);
    fflush(stdout);

    return ipc_frame_send(ctx->fd, type, payload, payload_len);
}

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s --socket PATH\n", prog);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int listener_fd = -1;
    struct sigaction sa;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }

    if (argc != 3 || strcmp(argv[1], "--socket") != 0) {
        usage(argv[0]);
        return 2;
    }

    path = argv[2];

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0 ||
        sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    listener_fd = uds_listener_open(path, 0600, 16);
    if (listener_fd < 0) {
        fprintf(stderr, "failed to open Unix socket %s: %s\n",
                path, strerror(errno));
        return 1;
    }

    printf("PHASE30_FRAMED_IPC_READY path=%s header=%u max_payload=%u\n",
           path,
           IPC_FRAME_HEADER_SIZE,
           IPC_FRAME_MAX_PAYLOAD);
    fflush(stdout);

    while (!g_stop) {
        uds_peer_cred_t cred;
        int client_fd = uds_accept_peer(listener_fd, &cred);
        if (client_fd < 0) {
            if (errno == EINTR && g_stop) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }

        printf("PHASE30_PEER pid=%ld uid=%ld gid=%ld\n",
               (long)cred.pid,
               (long)cred.uid,
               (long)cred.gid);
        fflush(stdout);

        ipc_frame_decoder_t decoder;
        echo_ctx_t ctx;
        uint8_t buf[4096];
        int protocol_error = 0;

        ipc_frame_decoder_init(&decoder);
        ctx.fd = client_fd;
        ctx.frames = 0;

        for (;;) {
            ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
            if (n > 0) {
                if (ipc_frame_decoder_feed(&decoder,
                                           buf,
                                           (size_t)n,
                                           echo_frame,
                                           &ctx) < 0) {
                    fprintf(stderr,
                            "PHASE30_PROTOCOL_ERROR errno=%d message=%s\n",
                            errno,
                            strerror(errno));
                    protocol_error = 1;
                    break;
                }
                continue;
            }
            if (n == 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            break;
        }

        close(client_fd);

        if (protocol_error) {
            continue;
        }
    }

    close(listener_fd);
    if (uds_cleanup_path(path) < 0) {
        fprintf(stderr, "cleanup failed for %s: %s\n", path, strerror(errno));
        return 1;
    }
    printf("PHASE30_FRAMED_IPC_CLEANUP_OK\n");
    fflush(stdout);
    return 0;
}
