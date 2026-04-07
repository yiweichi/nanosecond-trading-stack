#pragma once

#include <cstdint>
#include "common.h"
#include "market_data.h"

namespace nts {

struct PriceLevel {
    Price price       = 0.0;
    Qty   qty         = 0;
    Qty   order_count = 0;
};

class OrderBook {
public:
    static constexpr int MAX_LEVELS = MD_MAX_DEPTH;

    // ── Market data updates ──────────────────────────────────────
    void on_quote(const MdQuote& q);
    void on_depth(const MdDepth& d);
    void on_trade(const MdTrade& t);

    // ── L1 access ────────────────────────────────────────────────
    Price best_bid() const;
    Price best_ask() const;
    Qty   best_bid_qty() const;
    Qty   best_ask_qty() const;

    // ── L2 access ────────────────────────────────────────────────
    int               bid_depth() const { return bid_depth_; }
    int               ask_depth() const { return ask_depth_; }
    const PriceLevel& bid_level(int i) const { return bids_[i]; }
    const PriceLevel& ask_level(int i) const { return asks_[i]; }

    // ── Analytics ────────────────────────────────────────────────
    double mid_price() const;
    double spread() const;
    double spread_bps() const;

    // Qty imbalance across N levels: +1 = all bids, -1 = all asks
    double imbalance(int levels = 1) const;

    // Qty-weighted mid price across N levels (micro-price estimator)
    double weighted_mid(int levels = 3) const;

    // Volume-weighted average price to fill target_qty on given side
    double vwap(Side side, Qty target_qty) const;

    // Bid-to-ask qty ratio across N levels (>1 = buying pressure)
    double book_pressure(int levels = 5) const;

    Qty total_bid_qty(int levels = MAX_LEVELS) const;
    Qty total_ask_qty(int levels = MAX_LEVELS) const;

    // ── Last trade info ──────────────────────────────────────────
    Price    last_trade_price() const { return last_trade_price_; }
    Qty      last_trade_qty() const { return last_trade_qty_; }
    Side     last_trade_side() const { return last_trade_side_; }
    uint64_t last_trade_ts() const { return last_trade_ts_; }
    uint64_t trade_count() const { return trade_count_; }

    // ── Metadata ─────────────────────────────────────────────────
    uint64_t last_update_ts() const { return last_update_ts_; }
    uint64_t update_count() const { return update_count_; }
    bool     valid() const { return bid_depth_ > 0 && ask_depth_ > 0; }

    void clear();

private:
    PriceLevel bids_[MAX_LEVELS] = {};
    PriceLevel asks_[MAX_LEVELS] = {};
    int        bid_depth_        = 0;
    int        ask_depth_        = 0;

    Price    last_trade_price_ = 0.0;
    Qty      last_trade_qty_   = 0;
    Side     last_trade_side_  = Side::Buy;
    uint64_t last_trade_ts_    = 0;
    uint64_t trade_count_      = 0;

    uint64_t last_update_ts_ = 0;
    uint64_t update_count_   = 0;
};

}  // namespace nts
