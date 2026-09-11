#define _GNU_SOURCE
#include "telemetry_channel.h"

#include "ipc_frame.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct telemetry_frame_node {
    uint8_t *data;
    size_t len;
    size_t sent;
    struct telemetry_frame_node *next;
};

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if ((flags & O_NONBLOCK) != 0) {
        return 0;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int fill_address(const char *path, struct sockaddr_un *addr, socklen_t *len_out)
{
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

static void free_queue(telemetry_channel_t *channel, int count_as_dropped)
{
    telemetry_frame_node_t *node = channel->head;

    while (node != NULL) {
        telemetry_frame_node_t *next = node->next;
        if (count_as_dropped) {
            channel->dropped_frames++;
        }
        free(node->data);
        free(node);
        node = next;
    }

    channel->head = NULL;
    channel->tail = NULL;
    channel->queued_bytes = 0;
    channel->queued_frames = 0;
}

static void disconnect_channel(telemetry_channel_t *channel, int count_queue_as_dropped)
{
    if (channel->fd >= 0) {
        (void)close(channel->fd);
    }
    channel->fd = -1;
    channel->state = TELEMETRY_DISCONNECTED;
    free_queue(channel, count_queue_as_dropped);
}

void telemetry_channel_init(telemetry_channel_t *channel, size_t max_queue_bytes)
{
    if (channel == NULL) {
        return;
    }

    memset(channel, 0, sizeof(*channel));
    channel->fd = -1;
    channel->state = TELEMETRY_DISCONNECTED;
    channel->max_queue_bytes = max_queue_bytes;
}

void telemetry_channel_close(telemetry_channel_t *channel)
{
    if (channel == NULL) {
        return;
    }

    disconnect_channel(channel, 0);
}

int telemetry_channel_adopt_connected_fd(telemetry_channel_t *channel, int fd)
{
    if (channel == NULL || fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (channel->fd >= 0 || channel->state != TELEMETRY_DISCONNECTED) {
        errno = EALREADY;
        return -1;
    }

    if (set_nonblocking(fd) < 0) {
        return -1;
    }

    channel->fd = fd;
    channel->state = TELEMETRY_CONNECTED;
    return 0;
}

int telemetry_channel_connect(telemetry_channel_t *channel, const char *path)
{
    struct sockaddr_un addr;
    socklen_t addr_len;
    int fd;

    if (channel == NULL || channel->max_queue_bytes == 0) {
        errno = EINVAL;
        return -1;
    }
    if (channel->fd >= 0 || channel->state != TELEMETRY_DISCONNECTED) {
        errno = EALREADY;
        return -1;
    }
    if (fill_address(path, &addr, &addr_len) < 0) {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        channel->connect_failures++;
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, addr_len) == 0) {
        channel->fd = fd;
        channel->state = TELEMETRY_CONNECTED;
        return 0;
    }

    if (errno == EINPROGRESS) {
        channel->fd = fd;
        channel->state = TELEMETRY_CONNECTING;
        return 0;
    }

    {
        int saved_errno = errno;
        (void)close(fd);
        channel->connect_failures++;
        errno = saved_errno;
        return -1;
    }
}

static int finish_connect(telemetry_channel_t *channel)
{
    int error = 0;
    socklen_t len = sizeof(error);

    if (getsockopt(channel->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        channel->connect_failures++;
        disconnect_channel(channel, 1);
        return -1;
    }

    if (error != 0) {
        channel->connect_failures++;
        disconnect_channel(channel, 1);
        errno = error;
        return -1;
    }

    channel->state = TELEMETRY_CONNECTED;
    return 0;
}

int telemetry_channel_enqueue(telemetry_channel_t *channel,
                              uint8_t type,
                              const void *payload,
                              uint32_t payload_len)
{
    uint8_t header[IPC_FRAME_HEADER_SIZE];
    telemetry_frame_node_t *node;
    size_t frame_len;

    if (channel == NULL || channel->max_queue_bytes == 0 ||
        (payload_len > 0 && payload == NULL)) {
        errno = EINVAL;
        return -1;
    }

    if (ipc_frame_encode_header(type, payload_len, header) < 0) {
        return -1;
    }

    frame_len = IPC_FRAME_HEADER_SIZE + (size_t)payload_len;

    if (channel->state == TELEMETRY_DISCONNECTED ||
        frame_len > channel->max_queue_bytes ||
        channel->queued_bytes > channel->max_queue_bytes - frame_len) {
        channel->dropped_frames++;
        return 1;
    }

    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return -1;
    }

    node->data = malloc(frame_len);
    if (node->data == NULL) {
        free(node);
        return -1;
    }

    memcpy(node->data, header, IPC_FRAME_HEADER_SIZE);
    if (payload_len > 0) {
        memcpy(node->data + IPC_FRAME_HEADER_SIZE, payload, payload_len);
    }
    node->len = frame_len;

    if (channel->tail == NULL) {
        channel->head = node;
        channel->tail = node;
    } else {
        channel->tail->next = node;
        channel->tail = node;
    }

    channel->queued_bytes += frame_len;
    channel->queued_frames++;
    channel->enqueued_frames++;
    return 0;
}

int telemetry_channel_flush(telemetry_channel_t *channel)
{
    if (channel == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (channel->state == TELEMETRY_DISCONNECTED) {
        return 0;
    }

    if (channel->state == TELEMETRY_CONNECTING) {
        return 0;
    }

    while (channel->head != NULL) {
        telemetry_frame_node_t *node = channel->head;
        size_t remaining = node->len - node->sent;
        ssize_t n = send(channel->fd,
                         node->data + node->sent,
                         remaining,
                         MSG_DONTWAIT | MSG_NOSIGNAL);

        if (n > 0) {
            node->sent += (size_t)n;

            if (node->sent == node->len) {
                channel->queued_bytes -= node->len;
                channel->head = node->next;
                if (channel->head == NULL) {
                    channel->tail = NULL;
                }
                channel->queued_frames--;
                channel->sent_frames++;
                free(node->data);
                free(node);
            }
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            channel->would_block_count++;
            return 0;
        }

        disconnect_channel(channel, 1);
        return -1;
    }

    return 0;
}

int telemetry_channel_handle_events(telemetry_channel_t *channel, uint32_t events)
{
    if (channel == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (channel->state == TELEMETRY_DISCONNECTED) {
        return 0;
    }

    if ((events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
        if (channel->state == TELEMETRY_CONNECTING && (events & EPOLLOUT) != 0U) {
            if (finish_connect(channel) == 0) {
                return telemetry_channel_flush(channel);
            }
        }
        disconnect_channel(channel, 1);
        return -1;
    }

    if (channel->state == TELEMETRY_CONNECTING && (events & EPOLLOUT) != 0U) {
        if (finish_connect(channel) < 0) {
            return -1;
        }
    }

    if (channel->state == TELEMETRY_CONNECTED && (events & EPOLLOUT) != 0U) {
        return telemetry_channel_flush(channel);
    }

    return 0;
}

uint32_t telemetry_channel_epoll_events(const telemetry_channel_t *channel)
{
    uint32_t events = EPOLLERR | EPOLLHUP | EPOLLRDHUP;

    if (channel == NULL || channel->fd < 0 ||
        channel->state == TELEMETRY_DISCONNECTED) {
        return 0;
    }

    if (channel->state == TELEMETRY_CONNECTING || channel->head != NULL) {
        events |= EPOLLOUT;
    }

    return events;
}

const char *telemetry_channel_state_name(telemetry_state_t state)
{
    switch (state) {
    case TELEMETRY_DISCONNECTED:
        return "DISCONNECTED";
    case TELEMETRY_CONNECTING:
        return "CONNECTING";
    case TELEMETRY_CONNECTED:
        return "CONNECTED";
    default:
        return "UNKNOWN";
    }
}
