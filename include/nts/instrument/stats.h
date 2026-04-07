#pragma once

#include <cstdio>
#include <vector>
#include "tracer.h"

namespace nts::instrument {

struct LatencyStats {
    uint64_t min_ns    = 0;
    uint64_t max_ns    = 0;
    uint64_t mean_ns   = 0;
    uint64_t p50_ns    = 0;
    uint64_t p90_ns    = 0;
    uint64_t p99_ns    = 0;
    uint64_t p999_ns   = 0;
    double   stddev_ns = 0.0;
    size_t   samples   = 0;
};

struct Segment {
    Hop         from;
    Hop         to;
    const char* name;
};

class StatsCalculator {
public:
    // Compute latency distribution for a single hop pair.
    // Only traces that recorded both hops are included.
    static LatencyStats compute(const HopTracer& tracer, Hop from, Hop to);

    // Print full latency report (per-hop + end-to-end).
    static void print_report(const HopTracer& tracer, FILE* out = stdout);

    static const std::vector<Segment>& per_hop_segments();
    static const std::vector<Segment>& end_to_end_segments();
};

}  // namespace nts::instrument
