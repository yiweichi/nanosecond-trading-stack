#include "nts/exchange.h"
#include "nts/instrument/clock.h"

namespace nts {

void MockExchange::submit_order(const Order& order) {
    for (auto& slot : pending_) {
        if (!slot.active) {
            slot.order       = order;
            slot.ready_at_ns = instrument::now_ns() + latency_ns_;
            slot.active      = true;
            return;
        }
    }
    // All slots full — silently drop (acceptable for v1)
}

bool MockExchange::poll_ack(Ack& ack) {
    uint64_t now = instrument::now_ns();

    for (auto& slot : pending_) {
        if (slot.active && now >= slot.ready_at_ns) {
            ack.order_id   = slot.order.id;
            ack.filled     = true;   // always fill in v1
            ack.fill_price = slot.order.price;
            ack.ack_ts     = now;
            slot.active    = false;
            return true;
        }
    }

    return false;
}

} // namespace nts
