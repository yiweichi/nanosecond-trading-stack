#pragma once

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

struct TraceRecord {
    static constexpr size_t NUM_HOPS = static_cast<size_t>(Hop::COUNT);

    uint64_t timestamps[NUM_HOPS] = {};
    uint8_t  recorded_mask = 0;

    bool has(Hop h) const {
        return recorded_mask & (1u << static_cast<uint8_t>(h));
    }

    uint64_t latency(Hop from, Hop to) const {
        return timestamps[static_cast<size_t>(to)]
             - timestamps[static_cast<size_t>(from)];
    }
};

// Records nanosecond timestamps at each hop of the pipeline.
// Fixed-capacity, pre-allocated buffer. No allocation on the hot path.
class HopTracer {
public:
    static constexpr size_t DEFAULT_CAPACITY = 1u << 16; // 65536

    explicit HopTracer(size_t capacity = DEFAULT_CAPACITY);
    ~HopTracer() = default;

    HopTracer(const HopTracer&) = delete;
    HopTracer& operator=(const HopTracer&) = delete;

    void start_trace();
    void record(Hop hop);
    void end_trace();
    void discard_trace();

    void enable();
    void disable();
    bool is_enabled() const { return enabled_; }

    void reset();

    size_t count() const { return count_; }
    size_t capacity() const { return capacity_; }
    const TraceRecord& trace(size_t idx) const { return traces_[idx]; }

private:
    std::unique_ptr<TraceRecord[]> traces_;
    size_t capacity_;
    size_t count_    = 0;
    bool   enabled_  = true;
    bool   in_trace_ = false;
};

// Compile-time no-op tracer: every call inlines to nothing.
struct NoopTracer {
    void start_trace() {}
    void record(Hop) {}
    void end_trace() {}
    void discard_trace() {}
    void enable() {}
    void disable() {}
    bool is_enabled() const { return false; }
    void reset() {}
    size_t count() const { return 0; }
};

#ifdef NTS_ENABLE_TRACING
using ActiveTracer = HopTracer;
#else
using ActiveTracer = NoopTracer;
#endif

} // namespace nts::instrument
