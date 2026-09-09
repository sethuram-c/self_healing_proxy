#define _GNU_SOURCE
#include "uds.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void send_exact(int fd, const char *text) {
    size_t len = strlen(text);
    ssize_t n = send(fd, text, len, MSG_NOSIGNAL);
    assert(n == (ssize_t)len);
}

int main(void) {
    char dir_template[] = "/tmp/shp29-unit-XXXXXX";
    char path[256];
    char buffer[16] = {0};
    struct stat st;
    uds_peer_cred_t peer;
    pid_t child;
    int listener;
    int client;
    int status;
    ssize_t n;

    assert(mkdtemp(dir_template) != NULL);
    assert(snprintf(path, sizeof(path), "%s/telemetry.sock", dir_template) > 0);

    listener = uds_listener_open(path, 0600, 8);
    assert(listener >= 0);
    assert(lstat(path, &st) == 0);
    assert(S_ISSOCK(st.st_mode));
    assert((st.st_mode & 0777) == 0600);

    child = fork();
    assert(child >= 0);

    if (child == 0) {
        int fd = uds_connect_stream(path);
        if (fd < 0) {
            _exit(10);
        }
        send_exact(fd, "ping");
        n = recv(fd, buffer, sizeof(buffer), 0);
        if (n != 4 || memcmp(buffer, "pong", 4) != 0) {
            close(fd);
            _exit(11);
        }
        close(fd);
        _exit(0);
    }

    client = uds_accept_peer(listener, &peer);
    assert(client >= 0);
    assert(peer.pid == child);
    assert(peer.uid == getuid());
    assert(peer.gid == getgid());

    n = recv(client, buffer, sizeof(buffer), 0);
    assert(n == 4);
    assert(memcmp(buffer, "ping", 4) == 0);
    send_exact(client, "pong");
    close(client);

    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    close(listener);
    assert(uds_cleanup_path(path) == 0);
    assert(access(path, F_OK) < 0 && errno == ENOENT);
    assert(rmdir(dir_template) == 0);

    puts("unix domain socket helper tests passed");
    return 0;
}
