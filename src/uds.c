#define _GNU_SOURCE
#include "uds.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int fill_address(const char *path, struct sockaddr_un *addr, socklen_t *len_out) {
    size_t path_len;

    if (path == NULL || addr == NULL || len_out == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    path_len = strlen(path);
    if (path_len >= sizeof(addr->sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    memcpy(addr->sun_path, path, path_len + 1);
    *len_out = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1);
    return 0;
}

static int remove_stale_socket_if_safe(const char *path) {
    struct stat st;
    struct sockaddr_un addr;
    socklen_t addr_len;
    int probe_fd;
    int saved_errno;

    if (lstat(path, &st) < 0) {
        return errno == ENOENT ? 0 : -1;
    }

    if (!S_ISSOCK(st.st_mode)) {
        errno = EEXIST;
        return -1;
    }

    if (fill_address(path, &addr, &addr_len) < 0) {
        return -1;
    }

    probe_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (probe_fd < 0) {
        return -1;
    }

    if (connect(probe_fd, (struct sockaddr *)&addr, addr_len) == 0) {
        close(probe_fd);
        errno = EADDRINUSE;
        return -1;
    }

    saved_errno = errno;
    close(probe_fd);

    if (saved_errno != ECONNREFUSED && saved_errno != ENOENT) {
        errno = saved_errno;
        return -1;
    }

    if (lstat(path, &st) < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!S_ISSOCK(st.st_mode)) {
        errno = EEXIST;
        return -1;
    }

    return unlink(path);
}

int uds_listener_open(const char *path, mode_t mode, int backlog) {
    struct sockaddr_un addr;
    socklen_t addr_len;
    int fd;
    int saved_errno;

    if (backlog <= 0 || (mode & ~0777U) != 0U) {
        errno = EINVAL;
        return -1;
    }

    if (fill_address(path, &addr, &addr_len) < 0) {
        return -1;
    }

    if (remove_stale_socket_if_safe(path) < 0) {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    if (chmod(path, mode) < 0 || listen(fd, backlog) < 0) {
        saved_errno = errno;
        close(fd);
        (void)uds_cleanup_path(path);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

int uds_connect_stream(const char *path) {
    struct sockaddr_un addr;
    socklen_t addr_len;
    int fd;
    int saved_errno;

    if (fill_address(path, &addr, &addr_len) < 0) {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

int uds_accept_peer(int listener_fd, uds_peer_cred_t *peer_out) {
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    int fd;
    int saved_errno;

    if (listener_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    fd = accept4(listener_fd, NULL, NULL, SOCK_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    if (peer_out != NULL) {
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0) {
            saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return -1;
        }
        peer_out->pid = cred.pid;
        peer_out->uid = cred.uid;
        peer_out->gid = cred.gid;
    }

    return fd;
}

int uds_cleanup_path(const char *path) {
    struct stat st;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (lstat(path, &st) < 0) {
        return errno == ENOENT ? 0 : -1;
    }

    if (!S_ISSOCK(st.st_mode)) {
        errno = EEXIST;
        return -1;
    }

    return unlink(path);
}
