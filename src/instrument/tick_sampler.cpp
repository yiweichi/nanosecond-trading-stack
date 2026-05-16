#include "nts/instrument/tick_sampler.h"
#include "nts/instrument/clock.h"

#include <algorithm>

namespace nts::instrument {

void TickSampler::reserve() {
    samples.reserve(MAX_SAMPLES);
}

void TickSampler::add(uint64_t ticks) {
    if (samples.size() < MAX_SAMPLES) {
        samples.push_back(ticks);
    } else {
        dropped++;
    }
}

TickSampleStats compute_tick_stats(const TickSampler& sampler) {
    TickSampleStats stats;
    stats.samples = sampler.samples.size();
    stats.dropped = sampler.dropped;
    if (sampler.samples.empty()) return stats;

    std::vector<uint64_t> sorted = sampler.samples;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = 0;
    for (uint64_t v : sorted) sum += v;

    stats.min_ticks  = sorted.front();
    stats.max_ticks  = sorted.back();
    stats.mean_ticks = sum / sorted.size();
    stats.p50_ticks  = sorted[(sorted.size() - 1) / 2];
    stats.p99_ticks  = sorted[((sorted.size() - 1) * 99) / 100];
    return stats;
}

void print_tick_sampler_report(const char* name, const TickSampler& sampler, FILE* out) {
    const TickSampleStats stats = compute_tick_stats(sampler);
    if (stats.samples == 0) {
        fprintf(out, "[profile] %s: no samples", name);
        if (stats.dropped > 0) fprintf(out, " (%zu dropped)", stats.dropped);
        fprintf(out, "\n");
        return;
    }

    fprintf(out,
            "[profile] %s: samples=%zu dropped=%zu | ticks min=%llu p50=%llu mean=%llu "
            "p99=%llu max=%llu | ns p50=%llu p99=%llu max=%llu\n",
            name, stats.samples, stats.dropped, static_cast<unsigned long long>(stats.min_ticks),
            static_cast<unsigned long long>(stats.p50_ticks),
            static_cast<unsigned long long>(stats.mean_ticks),
            static_cast<unsigned long long>(stats.p99_ticks),
            static_cast<unsigned long long>(stats.max_ticks),
            static_cast<unsigned long long>(nts::instrument::ticks_to_ns(stats.p50_ticks)),
            static_cast<unsigned long long>(nts::instrument::ticks_to_ns(stats.p99_ticks)),
            static_cast<unsigned long long>(nts::instrument::ticks_to_ns(stats.max_ticks)));
}

}  // namespace nts::instrument
