#ifndef LATENCY_SIGNAL_H
#define LATENCY_SIGNAL_H

#include <stdint.h>

#define LATENCY_SIGNAL_ALPHA_DEN_MAX 1024U

typedef struct {
    uint32_t alpha_numerator;
    uint32_t alpha_denominator;
    uint64_t stale_after_ns;
} latency_signal_config_t;

typedef struct {
    latency_signal_config_t config;
    int has_sample;
    uint64_t ewma_ns;
    uint64_t last_sample_ns;
    uint64_t sample_count;
} latency_signal_t;

int latency_signal_init(latency_signal_t *signal, const latency_signal_config_t *config);
int latency_signal_record(latency_signal_t *signal, uint64_t latency_ns, uint64_t now_ns);
int latency_signal_get(const latency_signal_t *signal, uint64_t now_ns, uint64_t *ewma_ns);

#endif
