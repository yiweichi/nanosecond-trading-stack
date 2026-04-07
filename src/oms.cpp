#include "nts/oms.h"
#include "nts/instrument/clock.h"

namespace nts {

const Order* OMS::create_order(Side side, double price, uint32_t qty) {
    if (total_ >= MAX_ORDERS) return nullptr;

    uint64_t id   = next_id_++;
    size_t   slot = id % MAX_ORDERS;

    orders_[slot].id     = id;
    orders_[slot].price  = price;
    orders_[slot].qty    = qty;
    orders_[slot].side   = side;
    orders_[slot].status = OrderStatus::Pending;
    orders_[slot].sent_ts = instrument::now_ns();

    total_++;
    pending_++;

    return &orders_[slot];
}

void OMS::on_ack(const Ack& ack) {
    size_t slot = ack.order_id % MAX_ORDERS;
    Order& order = orders_[slot];

    if (order.id != ack.order_id || order.status != OrderStatus::Pending) {
        return;
    }

    if (ack.filled) {
        order.status = OrderStatus::Filled;
        filled_++;
        int32_t signed_qty = static_cast<int32_t>(order.qty);
        position_ += (order.side == Side::Buy) ? signed_qty : -signed_qty;
    } else {
        order.status = OrderStatus::Rejected;
    }

    pending_--;
}

} // namespace nts
