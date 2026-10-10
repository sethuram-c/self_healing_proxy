#ifndef ROUTING_POLICY_WATCHER_H
#define ROUTING_POLICY_WATCHER_H

#include <stdint.h>

#define ROUTING_POLICY_WATCHER_DEBOUNCE_MS 100ULL
#define ROUTING_POLICY_WATCHER_PATH_MAX 4096U
#define ROUTING_POLICY_WATCHER_NAME_MAX 256U

typedef struct {
    int inotify_fd;
    int watch_fd;
    char directory[ROUTING_POLICY_WATCHER_PATH_MAX];
    char filename[ROUTING_POLICY_WATCHER_NAME_MAX];
    int reload_pending;
    uint64_t last_event_ms;
} routing_policy_watcher_t;

int routing_policy_watcher_init(
    routing_policy_watcher_t *watcher,
    const char *policy_path
);

int routing_policy_watcher_fd(
    const routing_policy_watcher_t *watcher
);

int routing_policy_watcher_process(
    routing_policy_watcher_t *watcher,
    uint64_t now_ms,
    int *change_detected,
    int *reload_ready
);

int routing_policy_watcher_wait_timeout_ms(
    const routing_policy_watcher_t *watcher,
    uint64_t now_ms,
    int current_timeout_ms
);

int routing_policy_watcher_discard_pending(
    routing_policy_watcher_t *watcher
);

void routing_policy_watcher_destroy(
    routing_policy_watcher_t *watcher
);

#endif
