#pragma once
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace nts {
namespace matching {

using OrderId = uint64_t;
using Price   = uint64_t;
using Qty     = uint64_t;

enum class Side : uint8_t { Buy, Sell };
enum class OrderType : uint8_t { Limit, Market };

struct Order {
    OrderId   id;
    Side      side;
    Price     price;
    Qty       qty;
    OrderType type;
};

struct Fill {
    OrderId maker_id;
    OrderId taker_id;
    Price   price;
    Qty     qty;
    Side    side;
};

class OrderBook {
public:
    OrderBook() = default;

    void add_order(const Order& order, std::vector<Fill>& fills);
    bool cancel(OrderId order_id);

    std::optional<Price>    best_bid() const;
    std::optional<Price>    best_ask() const;
    std::optional<uint64_t> spread() const;

    size_t len() const { return locations_.size(); }
    bool   is_empty() const { return locations_.empty(); }

    Qty depth_at(Side side, Price price) const;

private:
    struct OrderNode {
        OrderId id;
        Qty     qty;
    };

    using OrderQueue = std::list<OrderNode>;

    struct Location {
        Side                 side;
        Price                price;
        OrderQueue::iterator it;
    };

    // bids: descending price (best bid = begin)
    std::map<Price, OrderQueue, std::greater<Price>> bids_;
    // asks: ascending price (best ask = begin)
    std::map<Price, OrderQueue> asks_;
    // O(1) cancel lookup
    std::unordered_map<OrderId, Location> locations_;

    void match_buy(const Order& order, Qty& remaining, std::vector<Fill>& fills);
    void match_sell(const Order& order, Qty& remaining, std::vector<Fill>& fills);
    void place(OrderId id, Side side, Price price, Qty qty);
};

}  // namespace matching
}  // namespace nts
