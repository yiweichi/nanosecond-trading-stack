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
    COUNT
};

const char* hop_name(Hop h);

// One cache line per trace — stores raw ticks, converts to ns on cold path.
struct alignas(64) TraceRecord {
    static constexpr size_t NUM_HOPS = static_cast<size_t>(Hop::COUNT);

    uint64_t ticks[NUM_HOPS];
    uint8_t  recorded_mask;

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
        if (__builtin_expect(!enabled_, 0)) return;  // NOLINT(readability-implicit-bool-conversion)
        in_trace_                          = true;
        traces_[write_pos()].recorded_mask = 0;
    }

    inline void record(Hop hop) {
        if (__builtin_expect(!in_trace_, 0)) return;  // NOLINT(readability-implicit-bool-conversion)
        auto pos                = write_pos();
        auto idx                = static_cast<size_t>(hop);
        traces_[pos].ticks[idx] = raw_ticks();
        traces_[pos].recorded_mask |= (1u << static_cast<uint8_t>(hop));
    }

    inline void end_trace() {
        if (__builtin_expect(!in_trace_, 0)) return;  // NOLINT(readability-implicit-bool-conversion)
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
