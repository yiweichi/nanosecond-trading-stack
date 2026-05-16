#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace nts::instrument {

struct TickSampleStats {
    uint64_t min_ticks  = 0;
    uint64_t max_ticks  = 0;
    uint64_t mean_ticks = 0;
    uint64_t p50_ticks  = 0;
    uint64_t p99_ticks  = 0;
    size_t   samples    = 0;
    size_t   dropped    = 0;
};

struct TickSampler {
    static constexpr size_t MAX_SAMPLES = 1u << 20;

    std::vector<uint64_t> samples;
    size_t                dropped = 0;

    void reserve();
    void add(uint64_t ticks);
};

TickSampleStats compute_tick_stats(const TickSampler& sampler);
void print_tick_sampler_report(const char* name, const TickSampler& sampler,
                               FILE* out = stderr);

}  // namespace nts::instrument
