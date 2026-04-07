#pragma once

#include "common.h"
#include <cstdint>
#include <cstddef>

namespace nts {

struct Order {
    uint64_t    id       = 0;
    double      price    = 0.0;
    uint64_t    sent_ts  = 0;
    uint32_t    qty      = 0;
    Side        side     = Side::Buy;
    OrderStatus status   = OrderStatus::Empty;
};

struct Ack {
    uint64_t order_id;
    double   fill_price;
    uint64_t ack_ts;
    bool     filled;
};

class OMS {
public:
    static constexpr size_t MAX_ORDERS = 4096;

    // Returns pointer to the created order, or nullptr if at capacity.
    const Order* create_order(Side side, double price, uint32_t qty);

    void on_ack(const Ack& ack);

    size_t  pending_count() const { return pending_; }
    size_t  filled_count()  const { return filled_; }
    size_t  total_orders()  const { return total_; }
    int32_t position()      const { return position_; }

private:
    Order    orders_[MAX_ORDERS] = {};
    uint64_t next_id_  = 1;
    size_t   total_    = 0;
    size_t   pending_  = 0;
    size_t   filled_   = 0;
    int32_t  position_ = 0;
};

} // namespace nts
