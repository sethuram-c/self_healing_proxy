#ifndef UDS_H
#define UDS_H

#include <sys/types.h>

typedef struct {
    pid_t pid;
    uid_t uid;
    gid_t gid;
} uds_peer_cred_t;

int uds_listener_open(const char *path, mode_t mode, int backlog);
int uds_connect_stream(const char *path);
int uds_accept_peer(int listener_fd, uds_peer_cred_t *peer_out);
int uds_cleanup_path(const char *path);

#endif
