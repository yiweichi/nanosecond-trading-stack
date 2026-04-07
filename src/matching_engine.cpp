#include "nts/matching_engine.h"
#include <algorithm>

namespace nts {
namespace matching {

void OrderBook::add_order(const Order& order, std::vector<Fill>& fills) {
    Qty remaining = order.qty;

    switch (order.side) {
        case Side::Buy: match_buy(order, remaining, fills); break;
        case Side::Sell: match_sell(order, remaining, fills); break;
    }

    if (remaining > 0 && order.type == OrderType::Limit) {
        place(order.id, order.side, order.price, remaining);
    }
}

bool OrderBook::cancel(OrderId order_id) {
    auto loc_it = locations_.find(order_id);
    if (loc_it == locations_.end()) return false;

    Side  side     = loc_it->second.side;
    Price price    = loc_it->second.price;
    auto  queue_it = loc_it->second.it;
    locations_.erase(loc_it);

    if (side == Side::Buy) {
        auto map_it = bids_.find(price);
        map_it->second.erase(queue_it);
        if (map_it->second.empty()) bids_.erase(map_it);
    } else {
        auto map_it = asks_.find(price);
        map_it->second.erase(queue_it);
        if (map_it->second.empty()) asks_.erase(map_it);
    }

    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<uint64_t> OrderBook::spread() const {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid || !ask) return std::nullopt;
    if (*ask < *bid) return std::nullopt;
    return *ask - *bid;
}

Qty OrderBook::depth_at(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) return 0;
        Qty total = 0;
        for (const auto& node : it->second) total += node.qty;
        return total;
    }
    auto it = asks_.find(price);
    if (it == asks_.end()) return 0;
    Qty total = 0;
    for (const auto& node : it->second) total += node.qty;
    return total;
}

void OrderBook::match_buy(const Order& order, Qty& remaining, std::vector<Fill>& fills) {
    while (remaining > 0 && !asks_.empty()) {
        auto  level_it  = asks_.begin();
        Price ask_price = level_it->first;

        if (order.type == OrderType::Limit && ask_price > order.price) return;

        auto& queue = level_it->second;
        while (remaining > 0 && !queue.empty()) {
            auto& maker    = queue.front();
            Qty   fill_qty = std::min(remaining, maker.qty);

            fills.push_back({maker.id, order.id, ask_price, fill_qty, Side::Buy});

            maker.qty -= fill_qty;
            remaining -= fill_qty;

            if (maker.qty == 0) {
                locations_.erase(maker.id);
                queue.pop_front();
            }
        }

        if (queue.empty()) {
            asks_.erase(level_it);
        }
    }
}

void OrderBook::match_sell(const Order& order, Qty& remaining, std::vector<Fill>& fills) {
    while (remaining > 0 && !bids_.empty()) {
        auto  level_it  = bids_.begin();
        Price bid_price = level_it->first;

        if (order.type == OrderType::Limit && bid_price < order.price) return;

        auto& queue = level_it->second;
        while (remaining > 0 && !queue.empty()) {
            auto& maker    = queue.front();
            Qty   fill_qty = std::min(remaining, maker.qty);

            fills.push_back({maker.id, order.id, bid_price, fill_qty, Side::Sell});

            maker.qty -= fill_qty;
            remaining -= fill_qty;

            if (maker.qty == 0) {
                locations_.erase(maker.id);
                queue.pop_front();
            }
        }

        if (queue.empty()) {
            bids_.erase(level_it);
        }
    }
}

void OrderBook::place(OrderId id, Side side, Price price, Qty qty) {
    if (side == Side::Buy) {
        auto& queue = bids_[price];
        queue.push_back({id, qty});
        locations_[id] = {side, price, std::prev(queue.end())};
    } else {
        auto& queue = asks_[price];
        queue.push_back({id, qty});
        locations_[id] = {side, price, std::prev(queue.end())};
    }
}

}  // namespace matching
}  // namespace nts
