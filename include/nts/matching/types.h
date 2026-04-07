#pragma once

#include <cstdint>

namespace nts::matching {

using OrderId = uint64_t;
using Price   = uint64_t;
using Qty     = uint64_t;

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t {
    Limit,
    Market,
};

struct Order {
    OrderId   id;
    Side      side;
    Price     price;
    Qty       qty;
    OrderType order_type;
};

struct Fill {
    OrderId maker_id;
    OrderId taker_id;
    Price   price;
    Qty     qty;
    Side    side;
};

}  // namespace nts::matching
