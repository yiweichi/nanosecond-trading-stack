#include "nts/instrument/stats.h"

#include <algorithm>
#include <cmath>
#include <cinttypes>
#include <numeric>

namespace nts::instrument {

// ── Segment definitions ─────────────────────────────────────────────────────

const std::vector<Segment>& StatsCalculator::per_hop_segments() {
    static const std::vector<Segment> segs = {
        {Hop::RecvStart,    Hop::RecvDone,     "RecvStart -> RecvDone"},
        {Hop::RecvDone,     Hop::BookUpdated,  "RecvDone -> BookUpdated"},
        {Hop::BookUpdated,  Hop::StrategyDone, "BookUpdated -> StrategyDone"},
        {Hop::StrategyDone, Hop::OrderSent,    "StrategyDone -> OrderSent"},
        {Hop::OrderSent,    Hop::AckReceived,  "OrderSent -> AckReceived"},
        {Hop::AckReceived,  Hop::AckProcessed, "AckReceived -> AckProcessed"},
    };
    return segs;
}

const std::vector<Segment>& StatsCalculator::end_to_end_segments() {
    static const std::vector<Segment> segs = {
        {Hop::RecvDone,  Hop::StrategyDone, "RecvDone -> StrategyDone (core)"},
        {Hop::RecvDone,  Hop::OrderSent,    "RecvDone -> OrderSent (tick-to-order)"},
        {Hop::RecvStart, Hop::OrderSent,    "RecvStart -> OrderSent (tick-to-trade)"},
        {Hop::OrderSent, Hop::AckProcessed, "OrderSent -> AckProcessed (exchange RT)"},
    };
    return segs;
}

// ── Statistics computation ──────────────────────────────────────────────────

LatencyStats StatsCalculator::compute(const HopTracer& tracer, Hop from, Hop to) {
    std::vector<uint64_t> latencies;
    latencies.reserve(tracer.count());

    for (size_t i = 0; i < tracer.count(); i++) {
        const auto& t = tracer.trace(i);
        if (t.has(from) && t.has(to)) {
            uint64_t dt = t.latency(from, to);
            latencies.push_back(dt);
        }
    }

    if (latencies.empty()) {
        return LatencyStats{};
    }

    std::sort(latencies.begin(), latencies.end());

    auto percentile = [&](double p) -> uint64_t {
        size_t idx = static_cast<size_t>(p * static_cast<double>(latencies.size() - 1));
        return latencies[idx];
    };

    LatencyStats stats;
    stats.samples = latencies.size();
    stats.min_ns  = latencies.front();
    stats.max_ns  = latencies.back();
    stats.p50_ns  = percentile(0.50);
    stats.p90_ns  = percentile(0.90);
    stats.p99_ns  = percentile(0.99);
    stats.p999_ns = percentile(0.999);

    double sum = 0.0;
    for (auto v : latencies) sum += static_cast<double>(v);
    double mean = sum / static_cast<double>(latencies.size());
    stats.mean_ns = static_cast<uint64_t>(mean);

    double sq_sum = 0.0;
    for (auto v : latencies) {
        double diff = static_cast<double>(v) - mean;
        sq_sum += diff * diff;
    }
    stats.stddev_ns = std::sqrt(sq_sum / static_cast<double>(latencies.size()));

    return stats;
}

// ── Report printing ─────────────────────────────────────────────────────────

static void print_header(FILE* out) {
    fprintf(out, "  %-42s %8s %8s %8s %8s %8s %8s %8s %8s %10s\n",
            "Segment", "Samples", "Min", "P50", "P90", "P99", "P99.9", "Max", "Mean", "StdDev");
    fprintf(out, "  ");
    for (int i = 0; i < 120; i++) fputc('-', out);
    fputc('\n', out);
}

static void print_stats_line(const char* name, const LatencyStats& stats, FILE* out) {
    if (stats.samples == 0) {
        fprintf(out, "  %-42s %8s\n", name, "(no data)");
        return;
    }
    fprintf(out, "  %-42s %8zu %8" PRIu64 " %8" PRIu64 " %8" PRIu64 " %8" PRIu64
                 " %8" PRIu64 " %8" PRIu64 " %8" PRIu64 " %10.1f\n",
            name,
            stats.samples,
            stats.min_ns, stats.p50_ns, stats.p90_ns, stats.p99_ns, stats.p999_ns,
            stats.max_ns, stats.mean_ns, stats.stddev_ns);
}

void StatsCalculator::print_report(const HopTracer& tracer, FILE* out) {
    fprintf(out, "\n");
    fprintf(out, "===============================================================================\n");
    fprintf(out, "              NANOSECOND TRADING STACK — LATENCY REPORT\n");
    fprintf(out, "===============================================================================\n\n");

    fprintf(out, "  Traces recorded: %zu / %zu\n\n", tracer.count(), tracer.capacity());

    // Count traces with specific characteristics
    size_t with_data = 0, with_orders = 0, with_acks = 0;
    for (size_t i = 0; i < tracer.count(); i++) {
        const auto& t = tracer.trace(i);
        if (t.has(Hop::RecvDone))     with_data++;
        if (t.has(Hop::OrderSent))    with_orders++;
        if (t.has(Hop::AckProcessed)) with_acks++;
    }
    fprintf(out, "  Traces with market data:  %zu\n", with_data);
    fprintf(out, "  Traces with orders:       %zu\n", with_orders);
    fprintf(out, "  Traces with acks:         %zu\n\n", with_acks);

    // Per-hop latency
    fprintf(out, "Per-Hop Latency (nanoseconds):\n");
    print_header(out);
    for (const auto& seg : per_hop_segments()) {
        auto stats = compute(tracer, seg.from, seg.to);
        print_stats_line(seg.name, stats, out);
    }
    fprintf(out, "\n");

    // End-to-end latency
    fprintf(out, "End-to-End Latency (nanoseconds):\n");
    print_header(out);
    for (const auto& seg : end_to_end_segments()) {
        auto stats = compute(tracer, seg.from, seg.to);
        print_stats_line(seg.name, stats, out);
    }

    fprintf(out, "\n===============================================================================\n\n");
}

} // namespace nts::instrument
