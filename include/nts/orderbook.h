#pragma once

#include <cstdint>
#include "common.h"
#include "market_data.h"

namespace nts {

struct PriceLevel {
    Price price = 0.0;
    Qty   qty   = 0;
};

class OrderBook {
public:
    static constexpr int MAX_LEVELS = MD_MAX_DEPTH;

    // ── Market data updates ──────────────────────────────────────
    void on_quote(const MdQuote& q);
    void on_reference(const MdReference& r);

    // ── L1 access ────────────────────────────────────────────────
    Price best_bid() const;
    Price best_ask() const;
    Price reference_mid() const { return reference_mid_; }
    bool  has_reference() const { return has_reference_; }

    // ── Analytics ────────────────────────────────────────────────
    double mid_price() const;

    bool valid() const { return bid_depth_ > 0 && ask_depth_ > 0; }

    void clear();

private:
    PriceLevel bids_[MAX_LEVELS] = {};
    PriceLevel asks_[MAX_LEVELS] = {};
    int        bid_depth_        = 0;
    int        ask_depth_        = 0;

    Price reference_mid_ = 0.0;
    bool  has_reference_ = false;
};

}  // namespace nts
