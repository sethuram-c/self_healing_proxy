#include "adaptive_score.h"

#include <stdint.h>

static uint32_t latency_ratio_component(uint64_t latency_ns, uint64_t fastest_ns)
{
    if (latency_ns == 0 || fastest_ns == 0 || latency_ns <= fastest_ns) {
        return ADAPTIVE_SCORE_SCALE;
    }

    uint64_t whole = latency_ns / fastest_ns;
    uint64_t rem = latency_ns % fastest_ns;
    if (whole >= ADAPTIVE_SCORE_MAX_LATENCY_RATIO / ADAPTIVE_SCORE_SCALE) {
        return ADAPTIVE_SCORE_MAX_LATENCY_RATIO;
    }

    uint64_t value = whole * ADAPTIVE_SCORE_SCALE;
    if (rem != 0) {
        uint64_t fractional = (rem * ADAPTIVE_SCORE_SCALE) / fastest_ns;
        value += fractional;
    }
    if (value > ADAPTIVE_SCORE_MAX_LATENCY_RATIO) {
        value = ADAPTIVE_SCORE_MAX_LATENCY_RATIO;
    }
    return (uint32_t)value;
}

static uint32_t load_component(size_t active_connections, size_t max_connections)
{
    if (max_connections == 0 || active_connections >= max_connections) {
        return ADAPTIVE_SCORE_SCALE;
    }
    size_t whole = active_connections / max_connections;
    size_t rem = active_connections % max_connections;
    uint64_t value = (uint64_t)whole * ADAPTIVE_SCORE_SCALE;
    value += ((uint64_t)rem * ADAPTIVE_SCORE_SCALE) / max_connections;
    if (value > ADAPTIVE_SCORE_SCALE) {
        value = ADAPTIVE_SCORE_SCALE;
    }
    return (uint32_t)value;
}

int adaptive_score_validate_config(const adaptive_score_config_t *config)
{
    if (config == NULL || config->latency_weight > ADAPTIVE_SCORE_MAX_WEIGHT
        || config->load_weight > ADAPTIVE_SCORE_MAX_WEIGHT
        || (config->latency_weight == 0 && config->load_weight == 0)) {
        return -1;
    }
    return 0;
}

int adaptive_score_compute(uint64_t latency_ns, int has_fresh_latency,
    uint64_t fastest_fresh_latency_ns, size_t active_connections,
    size_t max_connections, const adaptive_score_config_t *config,
    adaptive_score_result_t *result)
{
    if (adaptive_score_validate_config(config) != 0 || result == NULL
        || max_connections == 0 || active_connections > max_connections
        || (has_fresh_latency && (latency_ns == 0 || fastest_fresh_latency_ns == 0))) {
        return -1;
    }

    result->load_component = load_component(active_connections, max_connections);
    result->latency_component = 0;
    result->used_latency = 0;

    uint64_t numerator = (uint64_t)config->load_weight * result->load_component;
    uint64_t denominator = config->load_weight;

    if (has_fresh_latency && config->latency_weight != 0) {
        result->latency_component = latency_ratio_component(latency_ns,
            fastest_fresh_latency_ns);
        result->used_latency = 1;
        numerator += (uint64_t)config->latency_weight * result->latency_component;
        denominator += config->latency_weight;
    }

    if (denominator == 0) {
        /* latency-only policy with no fresh sample: neutral score */
        result->combined_score = ADAPTIVE_SCORE_SCALE;
        return 0;
    }

    result->combined_score = (uint32_t)(numerator / denominator);
    return 0;
}
