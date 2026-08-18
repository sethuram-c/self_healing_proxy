#include "net_utils.h"

#include <fcntl.h>

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
