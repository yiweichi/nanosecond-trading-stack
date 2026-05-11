#pragma once

#include "nts/instrument/clock.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace nts::instrument {

enum class Hop : uint8_t {
    RecvStart = 0,
    RecvDone,
    BookUpdated,
    StrategyDone,
    OrderSent,
    AckReceived,
    AckProcessed,
    FillReceived,
    FillProcessed,
    COUNT
};

const char* hop_name(Hop h);

// One cache line per trace — stores raw ticks, converts to ns on cold path.
struct alignas(64) TraceRecord {
    static constexpr size_t NUM_HOPS = static_cast<size_t>(Hop::COUNT);

    uint64_t ticks[NUM_HOPS];
    uint16_t recorded_mask;

    bool has(Hop h) const { return (recorded_mask & (1u << static_cast<uint8_t>(h))) != 0u; }

    uint64_t latency_ticks(Hop from, Hop to) const {
        return ticks[static_cast<size_t>(to)] - ticks[static_cast<size_t>(from)];
    }

    uint64_t latency(Hop from, Hop to) const { return ticks_to_ns(latency_ticks(from, to)); }
};

// ── Production HopTracer ─────────────────────────────────────────────────────
// All hot-path methods are inline in the header so the compiler can inline them
// at call sites (eliminates ~5 ns function-call overhead per hop).
//
// Ring buffer: when capacity is reached, oldest traces are overwritten instead
// of silently dropping new ones.
//
// Capacity is always rounded up to a power of two so the modulo operation
// becomes a single bitwise AND.

class HopTracer {
public:
    static constexpr size_t DEFAULT_CAPACITY = 1u << 16;  // 65536

    explicit HopTracer(size_t capacity = DEFAULT_CAPACITY);
    ~HopTracer() = default;

    HopTracer(const HopTracer&)            = delete;
    HopTracer& operator=(const HopTracer&) = delete;

    // ── Hot path (inline) ────────────────────────────────────────────────

    inline void start_trace() {
        if (__builtin_expect(!enabled_, 0) != 0) {  // NOLINT(readability-implicit-bool-conversion)
            return;
        }
        in_trace_                          = true;
        traces_[write_pos()].recorded_mask = 0;
    }

    inline void record(Hop hop) {
        if (__builtin_expect(!in_trace_, 0) != 0) {  // NOLINT(readability-implicit-bool-conversion)
            return;
        }
        record_at(hop, raw_ticks());
    }

    inline void record_at(Hop hop, uint64_t ticks) {
        if (__builtin_expect(!in_trace_, 0) != 0) {  // NOLINT(readability-implicit-bool-conversion)
            return;
        }
        auto pos                = write_pos();
        auto idx                = static_cast<size_t>(hop);
        traces_[pos].ticks[idx] = ticks;
        traces_[pos].recorded_mask |= (1u << static_cast<uint8_t>(hop));
    }

    inline void record_order_ack(uint64_t order_sent_ticks, uint64_t ack_received_ticks,
                                 uint64_t ack_processed_ticks) {
        if (__builtin_expect(!enabled_, 0) != 0) {  // NOLINT(readability-implicit-bool-conversion)
            return;
        }

        // If a market-data trace is currently open, append the ack trace after
        // it. The current pipeline records acks after all per-loop hops, so the
        // active trace will not receive more record() calls before end_trace().
        auto& trace         = traces_[(total_count_ + (in_trace_ ? 1u : 0u)) & capacity_mask_];
        trace.recorded_mask = 0;
        trace.ticks[static_cast<size_t>(Hop::OrderSent)]    = order_sent_ticks;
        trace.ticks[static_cast<size_t>(Hop::AckReceived)]  = ack_received_ticks;
        trace.ticks[static_cast<size_t>(Hop::AckProcessed)] = ack_processed_ticks;
        trace.recorded_mask = (1u << static_cast<uint8_t>(Hop::OrderSent)) |
                              (1u << static_cast<uint8_t>(Hop::AckReceived)) |
                              (1u << static_cast<uint8_t>(Hop::AckProcessed));
        total_count_++;
    }

    inline void record_ack_fill(uint64_t ack_received_ticks, uint64_t fill_received_ticks,
                                uint64_t fill_processed_ticks) {
        if (__builtin_expect(!enabled_, 0) != 0) {  // NOLINT(readability-implicit-bool-conversion)
            return;
        }

        auto& trace         = traces_[(total_count_ + (in_trace_ ? 1u : 0u)) & capacity_mask_];
        trace.recorded_mask = 0;
        trace.ticks[static_cast<size_t>(Hop::AckReceived)]   = ack_received_ticks;
        trace.ticks[static_cast<size_t>(Hop::FillReceived)]  = fill_received_ticks;
        trace.ticks[static_cast<size_t>(Hop::FillProcessed)] = fill_processed_ticks;
        trace.recorded_mask = (1u << static_cast<uint8_t>(Hop::AckReceived)) |
                              (1u << static_cast<uint8_t>(Hop::FillReceived)) |
                              (1u << static_cast<uint8_t>(Hop::FillProcessed));
        total_count_++;
    }

    inline void end_trace() {
        if (__builtin_expect(!in_trace_, 0) != 0) {  // NOLINT(readability-implicit-bool-conversion)
            return;
        }
        in_trace_ = false;
        total_count_++;
    }

    inline void discard_trace() { in_trace_ = false; }

    // ── Cold path ────────────────────────────────────────────────────────

    void enable();
    void disable();
    bool is_enabled() const { return enabled_; }

    void reset();

    size_t count() const { return total_count_ < capacity_ ? total_count_ : capacity_; }
    size_t capacity() const { return capacity_; }
    size_t total_traces() const { return total_count_; }
    bool   wrapped() const { return total_count_ > capacity_; }

    const TraceRecord& trace(size_t idx) const {
        if (total_count_ <= capacity_) {
            return traces_[idx];
        }
        size_t oldest = total_count_ & capacity_mask_;
        return traces_[(oldest + idx) & capacity_mask_];
    }

private:
    size_t write_pos() const { return total_count_ & capacity_mask_; }

    std::unique_ptr<TraceRecord[]> traces_;
    size_t                         capacity_;
    size_t                         capacity_mask_;
    size_t                         total_count_ = 0;
    bool                           enabled_     = true;
    bool                           in_trace_    = false;
};

// Compile-time no-op tracer: every call inlines to nothing.
struct NoopTracer {
    void   start_trace() {}
    void   record(Hop) {}
    void   record_at(Hop, uint64_t) {}
    void   record_order_ack(uint64_t, uint64_t, uint64_t) {}
    void   record_ack_fill(uint64_t, uint64_t, uint64_t) {}
    void   end_trace() {}
    void   discard_trace() {}
    void   enable() {}
    void   disable() {}
    bool   is_enabled() const { return false; }
    void   reset() {}
    size_t count() const { return 0; }
    size_t total_traces() const { return 0; }
};

#ifdef NTS_ENABLE_TRACING
using ActiveTracer = HopTracer;
#else
using ActiveTracer = NoopTracer;
#endif

}  // namespace nts::instrument
