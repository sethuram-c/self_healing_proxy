#ifndef HEALING_EVENT_H
#define HEALING_EVENT_H

#include <stddef.h>
#include <stdint.h>

#define HEALING_EVENT_JSON_MAX 1024U

typedef struct {
    uint64_t timestamp_ns;
    uint64_t sequence;
    const char *backend;
    const char *event;
    const char *health_state;
    const char *circuit_state;
    const char *recovery_state;
    const char *action;
    const char *reason;
} healing_event_t;

int healing_event_serialize(const healing_event_t *event, char *out, size_t out_size);

#endif
