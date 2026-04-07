#include "nts/matching/orderbook.h"

#include <algorithm>

namespace nts::matching {

// ── Public API ──────────────────────────────────────────────────────────────

void OrderBook::add_order(Order order, std::vector<Fill>& fills) {
    Qty remaining = order.qty;

    switch (order.side) {
        case Side::Buy: match_buy(order, remaining, fills); break;
        case Side::Sell: match_sell(order, remaining, fills); break;
    }

    if (remaining > 0 && order.order_type == OrderType::Limit) {
        place(order.id, order.side, order.price, remaining);
    }
}

bool OrderBook::cancel(OrderId order_id) {
    auto loc_it = locations_.find(order_id);
    if (loc_it == locations_.end()) return false;

    auto [side, price, order_iter] = loc_it->second;
    locations_.erase(loc_it);

    auto& book     = (side == Side::Buy) ? bids_ : asks_;
    auto  level_it = book.find(price);
    level_it->second.erase(order_iter);

    if (level_it->second.empty()) book.erase(level_it);

    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.rbegin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<uint64_t> OrderBook::spread() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (bb && ba && *ba >= *bb) return *ba - *bb;
    return std::nullopt;
}

size_t OrderBook::len() const {
    return locations_.size();
}

bool OrderBook::is_empty() const {
    return locations_.empty();
}

Qty OrderBook::depth_at(Side side, Price price) const {
    const auto& book = (side == Side::Buy) ? bids_ : asks_;
    auto        it   = book.find(price);
    if (it == book.end()) return 0;

    Qty total = 0;
    for (const auto& order : it->second) total += order.qty;
    return total;
}

// ── Matching ────────────────────────────────────────────────────────────────

void OrderBook::match_buy(const Order& order, Qty& remaining, std::vector<Fill>& fills) {
    while (remaining > 0 && !asks_.empty()) {
        auto it = asks_.begin();

        if (order.order_type == OrderType::Limit && it->first > order.price) return;

        fill_at_level(Side::Buy, it->first, order.id, remaining, fills);

        if (it->second.empty()) asks_.erase(it);
    }
}

void OrderBook::match_sell(const Order& order, Qty& remaining, std::vector<Fill>& fills) {
    while (remaining > 0 && !bids_.empty()) {
        auto it = std::prev(bids_.end());

        if (order.order_type == OrderType::Limit && it->first < order.price) return;

        fill_at_level(Side::Sell, it->first, order.id, remaining, fills);

        if (it->second.empty()) bids_.erase(it);
    }
}

void OrderBook::fill_at_level(Side taker_side, Price price, OrderId taker_id, Qty& remaining,
                              std::vector<Fill>& fills) {
    auto& book     = (taker_side == Side::Buy) ? asks_ : bids_;
    auto  level_it = book.find(price);
    if (level_it == book.end()) return;

    auto& orders = level_it->second;

    while (remaining > 0 && !orders.empty()) {
        auto& front    = orders.front();
        Qty   fill_qty = std::min(remaining, front.qty);
        front.qty -= fill_qty;
        remaining -= fill_qty;

        fills.push_back(Fill{front.id, taker_id, price, fill_qty, taker_side});

        if (front.qty == 0) {
            locations_.erase(front.id);
            orders.pop_front();
        }
    }
}

// ── Placement ───────────────────────────────────────────────────────────────

void OrderBook::place(OrderId id, Side side, Price price, Qty qty) {
    auto& book   = (side == Side::Buy) ? bids_ : asks_;
    auto& orders = book[price];
    orders.push_back(RestingOrder{id, qty});
    locations_[id] = OrderLocation{side, price, std::prev(orders.end())};
}

}  // namespace nts::matching
