#include "nts/instrument/tracer.h"

namespace nts::instrument {

const char* hop_name(Hop h) {
    switch (h) {
        case Hop::RecvStart: return "RecvStart";
        case Hop::RecvDone: return "RecvDone";
        case Hop::BookUpdated: return "BookUpdated";
        case Hop::StrategyDone: return "StrategyDone";
        case Hop::OrderSent: return "OrderSent";
        case Hop::AckReceived: return "AckReceived";
        case Hop::AckProcessed: return "AckProcessed";
        case Hop::COUNT: return "COUNT";
    }
    return "Unknown";
}

static size_t round_up_pow2(size_t v) {
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}

HopTracer::HopTracer(size_t capacity)
    : traces_(std::make_unique<TraceRecord[]>(round_up_pow2(capacity)))
    , capacity_(round_up_pow2(capacity))
    , capacity_mask_(round_up_pow2(capacity) - 1) {}

void HopTracer::enable() {
    enabled_ = true;
}
void HopTracer::disable() {
    enabled_ = false;
}

void HopTracer::reset() {
    total_count_ = 0;
    in_trace_    = false;
}

}  // namespace nts::instrument
