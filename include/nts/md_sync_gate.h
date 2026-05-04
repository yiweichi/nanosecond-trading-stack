#pragma once

#include <cstdint>

#include "common.h"

namespace nts {

inline constexpr uint64_t kMdSyncDefaultStaleQuoteWindowTicks = 10;

/// After a reference mid move, only treat quotes as in-sync inside a tick window.
class MdSyncGate {
public:
    explicit MdSyncGate(uint64_t stale_quote_window_ticks = kMdSyncDefaultStaleQuoteWindowTicks)
        : stale_quote_window_ticks_(stale_quote_window_ticks) {}

    void on_reference(uint64_t tick, Price reference_mid);
    void on_quote(uint64_t tick);
    bool allows() const;

private:
    uint64_t stale_quote_window_ticks_;
    uint64_t quote_tick_            = 0;
    uint64_t reference_change_tick_ = 0;
    Price    last_reference_mid_    = 0.0;
};

inline void MdSyncGate::on_reference(uint64_t tick, Price reference_mid) {
    if (reference_mid != last_reference_mid_) {
        reference_change_tick_ = tick;
    }
    last_reference_mid_ = reference_mid;
}

inline void MdSyncGate::on_quote(uint64_t tick) {
    quote_tick_ = tick;
}

inline bool MdSyncGate::allows() const {
    return quote_tick_ >= reference_change_tick_ &&
           quote_tick_ < reference_change_tick_ + stale_quote_window_ticks_;
}

}  // namespace nts
