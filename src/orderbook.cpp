#include "nts/orderbook.h"

namespace nts {

// ── Market data updates ──────────────────────────────────────────────────────

void OrderBook::on_quote(const MdQuote& q) {
    bid_depth_ = 1;
    bids_[0]   = {q.bid_price, q.bid_size};

    ask_depth_ = 1;
    asks_[0]   = {q.ask_price, q.ask_size};
}

void OrderBook::on_reference(const MdReference& r) {
    reference_mid_ = r.reference_mid;
    has_reference_ = true;
}

// ── L1 access ────────────────────────────────────────────────────────────────

Price OrderBook::best_bid() const {
    return bid_depth_ > 0 ? bids_[0].price : 0.0;
}

Price OrderBook::best_ask() const {
    return ask_depth_ > 0 ? asks_[0].price : 0.0;
}

// ── Analytics ────────────────────────────────────────────────────────────────

double OrderBook::mid_price() const {
    if (!valid()) return 0.0;
    return (bids_[0].price + asks_[0].price) * 0.5;
}

// ── Clear ────────────────────────────────────────────────────────────────────

void OrderBook::clear() {
    bid_depth_ = 0;
    ask_depth_ = 0;
    for (auto& l : bids_) l = {};
    for (auto& l : asks_) l = {};
    reference_mid_ = 0.0;
    has_reference_ = false;
}

}  // namespace nts
