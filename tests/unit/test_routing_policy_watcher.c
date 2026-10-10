#define _POSIX_C_SOURCE 200809L

#include "routing_policy_watcher.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void wait_for_event(
    routing_policy_watcher_t *watcher,
    uint64_t now_ms,
    int *detected
)
{
    for (int i = 0; i < 100; ++i) {
        int ready = 0;
        int status = routing_policy_watcher_process(
            watcher,
            now_ms,
            detected,
            &ready
        );
        assert(status == 0);
        if (*detected) {
            return;
        }
        struct timespec pause = {0, 1000000L};
        (void)nanosleep(&pause, NULL);
    }
    assert(!"timed out waiting for inotify event");
}

int main(void)
{
    char dir_template[] = "/tmp/phase60-watcher-XXXXXX";
    char *directory = mkdtemp(dir_template);
    assert(directory != NULL);

    char path[512];
    int written = snprintf(path, sizeof(path), "%s/routing-policy.conf", directory);
    assert(written > 0 && (size_t)written < sizeof(path));
    write_text(path, "score_slack=80\n");

    routing_policy_watcher_t watcher;
    assert(routing_policy_watcher_init(&watcher, path) == 0);
    assert(routing_policy_watcher_fd(&watcher) >= 0);

    int flags = fcntl(routing_policy_watcher_fd(&watcher), F_GETFL);
    assert(flags != -1);
    assert((flags & O_NONBLOCK) != 0);

    write_text(path, "score_slack=0\n");
    int detected = 0;
    wait_for_event(&watcher, 1000, &detected);
    assert(detected == 1);

    int ready = 0;
    assert(routing_policy_watcher_process(&watcher, 1099, &detected, &ready) == 0);
    assert(ready == 0);
    assert(routing_policy_watcher_wait_timeout_ms(&watcher, 1099, 500) == 1);

    assert(routing_policy_watcher_process(&watcher, 1100, &detected, &ready) == 0);
    assert(ready == 1);

    write_text(path, "score_slack=1\n");
    write_text(path, "score_slack=2\n");
    write_text(path, "score_slack=3\n");
    detected = 0;
    wait_for_event(&watcher, 2000, &detected);
    assert(routing_policy_watcher_process(&watcher, 2099, &detected, &ready) == 0);
    assert(ready == 0);
    assert(routing_policy_watcher_process(&watcher, 2100, &detected, &ready) == 0);
    assert(ready == 1);
    assert(routing_policy_watcher_process(&watcher, 2200, &detected, &ready) == 0);
    assert(ready == 0);

    assert(routing_policy_watcher_discard_pending(&watcher) == 0);
    routing_policy_watcher_destroy(&watcher);
    assert(routing_policy_watcher_fd(&watcher) == -1);

    routing_policy_watcher_t missing;
    char missing_path[512];
    written = snprintf(missing_path, sizeof(missing_path), "%s/missing.conf", directory);
    assert(written > 0 && (size_t)written < sizeof(missing_path));
    assert(routing_policy_watcher_init(&missing, missing_path) == ENOENT);

    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);

    puts("routing policy watcher tests passed");
    return 0;
}
