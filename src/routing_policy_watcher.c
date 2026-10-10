#define _POSIX_C_SOURCE 200809L

#include "routing_policy_watcher.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#define WATCH_MASK (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM)

static int split_policy_path(
    const char *path,
    char *directory,
    size_t directory_size,
    char *filename,
    size_t filename_size
)
{
    if (path == NULL || path[0] == '\0') {
        return EINVAL;
    }

    const char *slash = strrchr(path, '/');
    const char *base = slash == NULL ? path : slash + 1;

    if (base[0] == '\0') {
        return EINVAL;
    }

    size_t base_len = strlen(base);
    if (base_len >= filename_size) {
        return ENAMETOOLONG;
    }

    if (slash == NULL) {
        if (directory_size < 2U) {
            return ENAMETOOLONG;
        }
        strcpy(directory, ".");
    } else if (slash == path) {
        if (directory_size < 2U) {
            return ENAMETOOLONG;
        }
        strcpy(directory, "/");
    } else {
        size_t directory_len = (size_t)(slash - path);
        if (directory_len >= directory_size) {
            return ENAMETOOLONG;
        }
        memcpy(directory, path, directory_len);
        directory[directory_len] = '\0';
    }

    memcpy(filename, base, base_len + 1U);
    return 0;
}

static int drain_events(
    routing_policy_watcher_t *watcher,
    uint64_t now_ms,
    int mark_pending,
    int *change_detected
)
{
    char buffer[4096]
        __attribute__((aligned(__alignof__(struct inotify_event))));

    for (;;) {
        ssize_t count = read(watcher->inotify_fd, buffer, sizeof(buffer));

        if (count == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            return errno;
        }

        if (count == 0) {
            return EIO;
        }

        for (char *cursor = buffer; cursor < buffer + count;) {
            const struct inotify_event *event =
                (const struct inotify_event *)cursor;

            int matches = 0;
            if ((event->mask & IN_Q_OVERFLOW) != 0U) {
                matches = 1;
            } else if (event->len > 0U
                       && strcmp(event->name, watcher->filename) == 0) {
                matches = 1;
            }

            if (matches) {
                if (change_detected != NULL) {
                    *change_detected = 1;
                }
                if (mark_pending) {
                    watcher->reload_pending = 1;
                    watcher->last_event_ms = now_ms;
                }
            }

            cursor += sizeof(struct inotify_event) + event->len;
        }
    }
}

int routing_policy_watcher_init(
    routing_policy_watcher_t *watcher,
    const char *policy_path
)
{
    if (watcher == NULL) {
        return EINVAL;
    }

    memset(watcher, 0, sizeof(*watcher));
    watcher->inotify_fd = -1;
    watcher->watch_fd = -1;

    int status = split_policy_path(
        policy_path,
        watcher->directory,
        sizeof(watcher->directory),
        watcher->filename,
        sizeof(watcher->filename)
    );
    if (status != 0) {
        return status;
    }

    if (access(policy_path, F_OK) == -1) {
        return errno;
    }

    watcher->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watcher->inotify_fd == -1) {
        return errno;
    }

    watcher->watch_fd = inotify_add_watch(
        watcher->inotify_fd,
        watcher->directory,
        WATCH_MASK
    );
    if (watcher->watch_fd == -1) {
        status = errno;
        close(watcher->inotify_fd);
        watcher->inotify_fd = -1;
        return status;
    }

    return 0;
}

int routing_policy_watcher_fd(
    const routing_policy_watcher_t *watcher
)
{
    if (watcher == NULL) {
        return -1;
    }
    return watcher->inotify_fd;
}

int routing_policy_watcher_process(
    routing_policy_watcher_t *watcher,
    uint64_t now_ms,
    int *change_detected,
    int *reload_ready
)
{
    if (watcher == NULL || watcher->inotify_fd < 0 || reload_ready == NULL) {
        return EINVAL;
    }

    if (change_detected != NULL) {
        *change_detected = 0;
    }
    *reload_ready = 0;

    int status = drain_events(watcher, now_ms, 1, change_detected);
    if (status != 0) {
        return status;
    }

    if (!watcher->reload_pending) {
        return 0;
    }

    if (now_ms < watcher->last_event_ms
        || now_ms - watcher->last_event_ms < ROUTING_POLICY_WATCHER_DEBOUNCE_MS) {
        return 0;
    }

    watcher->reload_pending = 0;
    *reload_ready = 1;
    return 0;
}

int routing_policy_watcher_wait_timeout_ms(
    const routing_policy_watcher_t *watcher,
    uint64_t now_ms,
    int current_timeout_ms
)
{
    if (watcher == NULL || !watcher->reload_pending) {
        return current_timeout_ms;
    }

    uint64_t deadline = watcher->last_event_ms + ROUTING_POLICY_WATCHER_DEBOUNCE_MS;
    int watcher_timeout = 0;

    if (now_ms < deadline) {
        uint64_t remaining = deadline - now_ms;
        watcher_timeout = remaining > (uint64_t)INT32_MAX
            ? INT32_MAX
            : (int)remaining;
    }

    if (current_timeout_ms < 0 || watcher_timeout < current_timeout_ms) {
        return watcher_timeout;
    }
    return current_timeout_ms;
}

int routing_policy_watcher_discard_pending(
    routing_policy_watcher_t *watcher
)
{
    if (watcher == NULL || watcher->inotify_fd < 0) {
        return EINVAL;
    }

    int ignored = 0;
    int status = drain_events(watcher, 0, 0, &ignored);
    if (status != 0) {
        return status;
    }

    watcher->reload_pending = 0;
    watcher->last_event_ms = 0;
    return 0;
}

void routing_policy_watcher_destroy(
    routing_policy_watcher_t *watcher
)
{
    if (watcher == NULL) {
        return;
    }

    if (watcher->watch_fd >= 0 && watcher->inotify_fd >= 0) {
        (void)inotify_rm_watch(watcher->inotify_fd, watcher->watch_fd);
    }
    if (watcher->inotify_fd >= 0) {
        (void)close(watcher->inotify_fd);
    }

    watcher->inotify_fd = -1;
    watcher->watch_fd = -1;
    watcher->reload_pending = 0;
    watcher->last_event_ms = 0;
}
