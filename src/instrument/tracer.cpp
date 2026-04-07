#include "nts/instrument/tracer.h"
#include "nts/instrument/clock.h"

namespace nts::instrument {

const char* hop_name(Hop h) {
    switch (h) {
        case Hop::RecvStart:    return "RecvStart";
        case Hop::RecvDone:     return "RecvDone";
        case Hop::BookUpdated:  return "BookUpdated";
        case Hop::StrategyDone: return "StrategyDone";
        case Hop::OrderSent:    return "OrderSent";
        case Hop::AckReceived:  return "AckReceived";
        case Hop::AckProcessed: return "AckProcessed";
        case Hop::COUNT:        return "COUNT";
    }
    return "Unknown";
}

HopTracer::HopTracer(size_t capacity)
    : traces_(std::make_unique<TraceRecord[]>(capacity))
    , capacity_(capacity)
{}

void HopTracer::start_trace() {
    if (!enabled_ || count_ >= capacity_) return;
    in_trace_ = true;
    traces_[count_] = TraceRecord{};
}

void HopTracer::record(Hop hop) {
    if (!in_trace_) return;
    auto idx = static_cast<size_t>(hop);
    traces_[count_].timestamps[idx] = now_ns();
    traces_[count_].recorded_mask |= (1u << static_cast<uint8_t>(hop));
}

void HopTracer::end_trace() {
    if (!in_trace_) return;
    in_trace_ = false;
    count_++;
}

void HopTracer::discard_trace() {
    in_trace_ = false;
}

void HopTracer::enable()  { enabled_ = true; }
void HopTracer::disable() { enabled_ = false; }

void HopTracer::reset() {
    count_ = 0;
    in_trace_ = false;
}

} // namespace nts::instrument
