#include "net_utils.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int would_block(int error)
{
    return error == EAGAIN
        || error == EWOULDBLOCK;
}

static void test_nonblocking_flag(void)
{
    int sockets[2];

    assert(
        socketpair(
            AF_UNIX,
            SOCK_STREAM,
            0,
            sockets
        ) == 0
    );

    assert(set_nonblocking(sockets[0]) == 0);

    int flags = fcntl(
        sockets[0],
        F_GETFL,
        0
    );

    assert(flags != -1);

    assert(
        (flags & O_NONBLOCK) != 0
    );

    close(sockets[0]);
    close(sockets[1]);
}

static void test_recv_returns_eagain(void)
{
    int sockets[2];

    assert(
        socketpair(
            AF_UNIX,
            SOCK_STREAM,
            0,
            sockets
        ) == 0
    );

    assert(set_nonblocking(sockets[0]) == 0);

    char buffer[16];

    errno = 0;

    ssize_t received = recv(
        sockets[0],
        buffer,
        sizeof(buffer),
        0
    );

    assert(received == -1);
    assert(would_block(errno));

    const char payload[] = "abc";

    ssize_t sent = send(
        sockets[1],
        payload,
        sizeof(payload) - 1,
        MSG_NOSIGNAL
    );

    assert(sent == 3);

    received = recv(
        sockets[0],
        buffer,
        sizeof(buffer),
        0
    );

    assert(received == 3);

    assert(
        memcmp(
            buffer,
            payload,
            3
        ) == 0
    );

    close(sockets[0]);
    close(sockets[1]);
}

static void test_accept_returns_eagain(void)
{
    int listener = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    assert(listener != -1);

    struct sockaddr_in address;

    memset(
        &address,
        0,
        sizeof(address)
    );

    address.sin_family = AF_INET;

    address.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);

    /*
     * Port 0 asks Linux to choose an available
     * ephemeral port for this test.
     */
    address.sin_port = htons(0);

    assert(
        bind(
            listener,
            (struct sockaddr *)&address,
            sizeof(address)
        ) == 0
    );

    assert(
        listen(listener, 8) == 0
    );

    assert(
        set_nonblocking(listener) == 0
    );

    errno = 0;

    int client_fd = accept(
        listener,
        NULL,
        NULL
    );

    assert(client_fd == -1);
    assert(would_block(errno));

    close(listener);
}

int main(void)
{
    test_nonblocking_flag();

    test_recv_returns_eagain();

    test_accept_returns_eagain();

    puts(
        "nonblocking socket tests passed"
    );

    return 0;
}
