#include "net_utils.h"

#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>


nonblock_connect_result_t
start_nonblocking_connect_ipv4(
    const char *address,
    uint16_t port,
    int *fd_out
)
{
    if (address == NULL
        || fd_out == NULL) {

        errno = EINVAL;

        return NONBLOCK_CONNECT_ERROR;
    }

    *fd_out = -1;

    int fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (fd == -1) {
        return NONBLOCK_CONNECT_ERROR;
    }

    if (set_nonblocking(fd) == -1) {
        int saved_errno = errno;

        close(fd);

        errno = saved_errno;

        return NONBLOCK_CONNECT_ERROR;
    }

    struct sockaddr_in backend;

    backend.sin_family = AF_INET;
    backend.sin_port = htons(port);

    int converted = inet_pton(
        AF_INET,
        address,
        &backend.sin_addr
    );

    if (converted != 1) {
        close(fd);

        errno = EINVAL;

        return NONBLOCK_CONNECT_ERROR;
    }

    int result = connect(
        fd,
        (struct sockaddr *)&backend,
        sizeof(backend)
    );

    if (result == 0) {
        *fd_out = fd;

        return NONBLOCK_CONNECT_CONNECTED;
    }

    if (errno == EINPROGRESS) {
        *fd_out = fd;

        return NONBLOCK_CONNECT_IN_PROGRESS;
    }

    int saved_errno = errno;

    close(fd);

    errno = saved_errno;

    return NONBLOCK_CONNECT_ERROR;
}


int finish_nonblocking_connect(
    int fd
)
{
    int socket_error = 0;

    socklen_t length =
        sizeof(socket_error);

    if (getsockopt(
            fd,
            SOL_SOCKET,
            SO_ERROR,
            &socket_error,
            &length) == -1) {

        return -1;
    }

    if (socket_error != 0) {
        errno = socket_error;

        return -1;
    }

    return 0;
}

int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        return -1;
    }

    if ((flags & O_NONBLOCK) != 0) {
        return 0;
    }

    return fcntl(
        fd,
        F_SETFL,
        flags | O_NONBLOCK
    );
}
