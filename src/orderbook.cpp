#include "nts/orderbook.h"

#include <algorithm>

namespace nts {

// ── Market data updates ──────────────────────────────────────────────────────

void OrderBook::on_quote(const MdQuote& q) {
    bid_depth_ = 1;
    bids_[0]   = {q.bid_price, q.bid_size, 1};

    ask_depth_ = 1;
    asks_[0]   = {q.ask_price, q.ask_size, 1};

    last_update_ts_ = q.header.timestamp_ns;
    update_count_++;
}

void OrderBook::on_reference(const MdReference& r) {
    reference_mid_ = r.reference_mid;
    has_reference_ = true;
    last_update_ts_ = r.header.timestamp_ns;
    update_count_++;
}

// ── L1 access ────────────────────────────────────────────────────────────────

Price OrderBook::best_bid() const {
    return bid_depth_ > 0 ? bids_[0].price : 0.0;
}

Price OrderBook::best_ask() const {
    return ask_depth_ > 0 ? asks_[0].price : 0.0;
}

Qty OrderBook::best_bid_qty() const {
    return bid_depth_ > 0 ? bids_[0].qty : 0;
}

Qty OrderBook::best_ask_qty() const {
    return ask_depth_ > 0 ? asks_[0].qty : 0;
}

// ── Analytics ────────────────────────────────────────────────────────────────

double OrderBook::mid_price() const {
    if (!valid()) return 0.0;
    return (bids_[0].price + asks_[0].price) * 0.5;
}

double OrderBook::spread() const {
    if (!valid()) return 0.0;
    return asks_[0].price - bids_[0].price;
}

double OrderBook::spread_bps() const {
    double mid = mid_price();
    if (mid <= 0.0) return 0.0;
    return (spread() / mid) * 10000.0;
}

double OrderBook::imbalance(int levels) const {
    Qty    bid_total = total_bid_qty(levels);
    Qty    ask_total = total_ask_qty(levels);
    double total     = static_cast<double>(bid_total) + static_cast<double>(ask_total);
    if (total == 0.0) return 0.0;
    return (static_cast<double>(bid_total) - static_cast<double>(ask_total)) / total;
}

double OrderBook::weighted_mid(int levels) const {
    if (!valid()) return 0.0;

    int n = std::min(levels, std::min(bid_depth_, ask_depth_));
    if (n == 0) return mid_price();

    double bid_pq = 0.0, ask_pq = 0.0;
    double bid_q = 0.0, ask_q = 0.0;

    for (int i = 0; i < n; i++) {
        auto bq = static_cast<double>(bids_[i].qty);
        auto aq = static_cast<double>(asks_[i].qty);
        bid_pq += bids_[i].price * bq;
        ask_pq += asks_[i].price * aq;
        bid_q += bq;
        ask_q += aq;
    }

    double total = bid_q + ask_q;
    if (total == 0.0) return mid_price();

    // Weight bid VWAP by ask qty and vice versa:
    // large ask qty -> price more likely at bid side
    double bid_vwap = bid_pq / bid_q;
    double ask_vwap = ask_pq / ask_q;
    return (bid_vwap * ask_q + ask_vwap * bid_q) / total;
}

double OrderBook::vwap(Side side, Qty target_qty) const {
    const PriceLevel* levels = (side == Side::Buy) ? asks_ : bids_;
    int               depth  = (side == Side::Buy) ? ask_depth_ : bid_depth_;

    if (depth == 0 || target_qty == 0) return 0.0;

    double cost      = 0.0;
    Qty    remaining = target_qty;

    for (int i = 0; i < depth && remaining > 0; i++) {
        Qty fill = std::min(remaining, levels[i].qty);
        cost += levels[i].price * static_cast<double>(fill);
        remaining -= fill;
    }

    Qty filled = target_qty - remaining;
    if (filled == 0) return 0.0;
    return cost / static_cast<double>(filled);
}

double OrderBook::book_pressure(int levels) const {
    Qty bid_total = total_bid_qty(levels);
    Qty ask_total = total_ask_qty(levels);
    if (ask_total == 0) return bid_total > 0 ? 999.0 : 1.0;
    return static_cast<double>(bid_total) / static_cast<double>(ask_total);
}

Qty OrderBook::total_bid_qty(int levels) const {
    int n     = std::min(levels, bid_depth_);
    Qty total = 0;
    for (int i = 0; i < n; i++) total += bids_[i].qty;
    return total;
}

Qty OrderBook::total_ask_qty(int levels) const {
    int n     = std::min(levels, ask_depth_);
    Qty total = 0;
    for (int i = 0; i < n; i++) total += asks_[i].qty;
    return total;
}

// ── Clear ────────────────────────────────────────────────────────────────────

void OrderBook::clear() {
    bid_depth_ = 0;
    ask_depth_ = 0;
    for (auto& l : bids_) l = {};
    for (auto& l : asks_) l = {};
    reference_mid_    = 0.0;
    has_reference_    = false;
    last_update_ts_   = 0;
    update_count_     = 0;
}

}  // namespace nts
