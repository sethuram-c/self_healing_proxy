#include "latency_signal.h"

#include <stddef.h>

static uint64_t scaled_step(uint64_t delta, uint32_t numerator, uint32_t denominator)
{
    uint64_t whole = delta / denominator;
    uint64_t rem = delta % denominator;
    uint64_t step = whole * numerator + (rem * numerator) / denominator;
    if (step == 0 && delta != 0) {
        step = 1;
    }
    return step;
}

int latency_signal_init(latency_signal_t *signal, const latency_signal_config_t *config)
{
    if (signal == NULL || config == NULL || config->alpha_numerator == 0
        || config->alpha_denominator == 0
        || config->alpha_numerator > config->alpha_denominator
        || config->alpha_denominator > LATENCY_SIGNAL_ALPHA_DEN_MAX
        || config->stale_after_ns == 0) {
        return -1;
    }

    signal->config = *config;
    signal->has_sample = 0;
    signal->ewma_ns = 0;
    signal->last_sample_ns = 0;
    signal->sample_count = 0;
    return 0;
}

int latency_signal_record(latency_signal_t *signal, uint64_t latency_ns, uint64_t now_ns)
{
    if (signal == NULL || latency_ns == 0) {
        return -1;
    }
    if (signal->has_sample && now_ns < signal->last_sample_ns) {
        return -1;
    }

    if (!signal->has_sample) {
        signal->ewma_ns = latency_ns;
        signal->has_sample = 1;
    } else if (latency_ns >= signal->ewma_ns) {
        uint64_t delta = latency_ns - signal->ewma_ns;
        signal->ewma_ns += scaled_step(delta, signal->config.alpha_numerator,
            signal->config.alpha_denominator);
    } else {
        uint64_t delta = signal->ewma_ns - latency_ns;
        signal->ewma_ns -= scaled_step(delta, signal->config.alpha_numerator,
            signal->config.alpha_denominator);
    }

    signal->last_sample_ns = now_ns;
    signal->sample_count++;
    return 0;
}

int latency_signal_get(const latency_signal_t *signal, uint64_t now_ns, uint64_t *ewma_ns)
{
    if (signal == NULL || ewma_ns == NULL) {
        return -1;
    }
    if (!signal->has_sample) {
        return 0;
    }
    if (now_ns < signal->last_sample_ns) {
        return -1;
    }
    if (now_ns - signal->last_sample_ns > signal->config.stale_after_ns) {
        return 0;
    }
    *ewma_ns = signal->ewma_ns;
    return 1;
}
