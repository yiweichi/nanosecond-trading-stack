#pragma once

#include "types.h"

#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace nts::matching {

class OrderBook {
public:
    OrderBook() = default;

    /// Submit an order. Match against resting orders, then place remainder
    /// (limit only). Fills are appended to the caller-owned buffer.
    void add_order(Order order, std::vector<Fill>& fills);

    /// Cancel a resting order by ID. Returns true if the order existed.
    bool cancel(OrderId order_id);

    /// Best bid price (highest resting buy), or nullopt if bid side empty.
    std::optional<Price> best_bid() const;

    /// Best ask price (lowest resting sell), or nullopt if ask side empty.
    std::optional<Price> best_ask() const;

    /// Spread = best_ask - best_bid. Nullopt if either side empty.
    std::optional<uint64_t> spread() const;

    /// Number of resting orders on the book.
    size_t len() const;
    bool   is_empty() const;

    /// Total resting quantity at a given price on a given side.
    Qty depth_at(Side side, Price price) const;

private:
    struct RestingOrder {
        OrderId id;
        Qty     qty;
    };

    using OrderQueue = std::list<RestingOrder>;

    struct OrderLocation {
        Side                 side;
        Price                price;
        OrderQueue::iterator iter;
    };

    // Bids sorted ascending by key; best bid = rbegin (highest).
    // Asks sorted ascending by key; best ask = begin  (lowest).
    std::map<Price, OrderQueue>                bids_;
    std::map<Price, OrderQueue>                asks_;
    std::unordered_map<OrderId, OrderLocation> locations_;

    void match_buy(const Order& order, Qty& remaining, std::vector<Fill>& fills);
    void match_sell(const Order& order, Qty& remaining, std::vector<Fill>& fills);
    void fill_at_level(Side taker_side, Price price, OrderId taker_id, Qty& remaining,
                       std::vector<Fill>& fills);
    void place(OrderId id, Side side, Price price, Qty qty);
};

}  // namespace nts::matching
