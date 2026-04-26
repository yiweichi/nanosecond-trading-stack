#include "nts/oms.h"
#include "nts/instrument/clock.h"

#include <algorithm>
#include <cmath>

namespace nts {

// ── Hash map internals ──────────────────────────────────────────────────────

size_t OMS::map_hash(OrderId id) const {
    // Fibonacci hashing — ORDER_MAP_SIZE = 8192 = 2^13
    return static_cast<size_t>((id * 11400714819323198485ULL) >> (64 - 13));
}

size_t OMS::map_find(OrderId id) const {
    size_t idx = map_hash(id);
    for (size_t i = 0; i < ORDER_MAP_SIZE; i++) {
        size_t pos = (idx + i) & (ORDER_MAP_SIZE - 1);
        if (!order_map_[pos].occupied) return ORDER_MAP_SIZE;
        if (order_map_[pos].id == id) return pos;
    }
    return ORDER_MAP_SIZE;
}

void OMS::map_insert(OrderId id, uint32_t slot) {
    size_t idx = map_hash(id);
    for (size_t i = 0; i < ORDER_MAP_SIZE; i++) {
        size_t pos = (idx + i) & (ORDER_MAP_SIZE - 1);
        if (!order_map_[pos].occupied) {
            order_map_[pos] = {id, slot, true};
            return;
        }
    }
}

void OMS::map_remove(OrderId id) {
    size_t pos = map_find(id);
    if (pos >= ORDER_MAP_SIZE) return;

    order_map_[pos].occupied = false;
    order_map_[pos].id       = 0;

    // Rehash subsequent displaced entries (Robin Hood deletion)
    size_t next = (pos + 1) & (ORDER_MAP_SIZE - 1);
    while (order_map_[next].occupied) {
        MapEntry tmp              = order_map_[next];
        order_map_[next].occupied = false;
        order_map_[next].id       = 0;
        map_insert(tmp.id, tmp.slot);
        next = (next + 1) & (ORDER_MAP_SIZE - 1);
    }
}

// ── Slot management ─────────────────────────────────────────────────────────

size_t OMS::alloc_slot() {
    if (free_top_ > 0) return free_stack_[--free_top_];
    if (next_fresh_slot_ < MAX_ORDERS) return next_fresh_slot_++;
    return MAX_ORDERS;  // sentinel: full
}

void OMS::free_slot(size_t slot, OrderId id) {
    map_remove(id);
    free_stack_[free_top_++] = slot;
}

// ── Order queries ───────────────────────────────────────────────────────────

const Order* OMS::find_order(OrderId id) const {
    size_t pos = map_find(id);
    if (pos >= ORDER_MAP_SIZE) return nullptr;
    return &orders_[order_map_[pos].slot];
}

Order* OMS::find_order(OrderId id) {
    size_t pos = map_find(id);
    if (pos >= ORDER_MAP_SIZE) return nullptr;
    return &orders_[order_map_[pos].slot];
}

// ── Risk check ──────────────────────────────────────────────────────────────

bool OMS::check_risk(Side side, Price price, Qty qty) const {
    if (qty > risk_.max_order_qty) return false;

    if (live_orders_ + pending_new_ >= risk_.max_live_orders) return false;

    // Include pending fills in position projection to prevent overshoot
    int32_t base = position_ + pending_position_delta_;
    base += (side == Side::Buy) ? static_cast<int32_t>(qty) : -static_cast<int32_t>(qty);
    if (std::abs(base) > risk_.max_position) return false;

    if (ref_price_ > 0.0 && risk_.max_price_deviation > 0.0) {
        if (std::abs(price - ref_price_) > risk_.max_price_deviation) return false;
    }

    return true;
}

// ── Order management ────────────────────────────────────────────────────────

Order* OMS::send_new(Side side, Price price, Qty qty, OrderType type) {
    if (!check_risk(side, price, qty)) return nullptr;

    size_t slot = alloc_slot();
    if (slot >= MAX_ORDERS) return nullptr;

    OrderId  id = next_id_++;
    uint64_t ts = instrument::now_ns();

    Order& o         = orders_[slot];
    o.id             = id;
    o.price          = price;
    o.qty            = qty;
    o.filled_qty     = 0;
    o.leaves_qty     = qty;
    o.side           = side;
    o.type           = type;
    o.status         = OrderStatus::Sent;
    o.create_ts      = ts;
    o.sent_ts        = ts;
    o.last_update_ts = ts;
    o.avg_fill_price = 0.0;

    map_insert(id, static_cast<uint32_t>(slot));
    order_count_++;
    pending_new_++;
    pending_position_delta_ +=
        (side == Side::Buy) ? static_cast<int32_t>(qty) : -static_cast<int32_t>(qty);

    return &o;
}

bool OMS::send_cancel(OrderId order_id) {
    Order* o = find_order(order_id);
    if (o == nullptr) return false;

    if (o->status != OrderStatus::Live && o->status != OrderStatus::PartiallyFilled) {
        return false;
    }

    o->status         = OrderStatus::PendingCancel;
    o->last_update_ts = instrument::now_ns();
    pending_cxl_++;

    return true;
}

// ── Execution processing ────────────────────────────────────────────────────

void OMS::on_execution(const ExecutionReport& report) {
    size_t map_pos = map_find(report.order_id);
    if (map_pos >= ORDER_MAP_SIZE) return;

    uint32_t slot = order_map_[map_pos].slot;
    Order&   o    = orders_[slot];

    switch (report.exec_type) {
        case ExecType::NewAck: {
            if (o.status != OrderStatus::Sent) return;
            o.status         = OrderStatus::Live;
            o.last_update_ts = report.timestamp_ns;
            if (pending_new_ > 0) pending_new_--;
            accepted_orders_++;
            live_orders_++;
            break;
        }

        case ExecType::Fill:
        case ExecType::PartialFill: {
            if (o.status != OrderStatus::Live && o.status != OrderStatus::PartiallyFilled &&
                o.status != OrderStatus::Sent) {
                return;
            }

            // Fill before explicit NewAck (common in practice)
            if (o.status == OrderStatus::Sent) {
                if (pending_new_ > 0) pending_new_--;
                accepted_orders_++;
                live_orders_++;
            }

            Qty    fq       = report.fill_qty;
            double old_cost = o.avg_fill_price * static_cast<double>(o.filled_qty);
            double new_cost = old_cost + report.fill_price * static_cast<double>(fq);

            o.filled_qty += fq;
            o.leaves_qty = (o.qty > o.filled_qty) ? (o.qty - o.filled_qty) : 0;

            if (o.filled_qty > 0) o.avg_fill_price = new_cost / static_cast<double>(o.filled_qty);

            bool fully_filled = (report.exec_type == ExecType::Fill || o.leaves_qty == 0);
            o.status          = fully_filled ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
            o.last_update_ts  = report.timestamp_ns;

            // Transfer filled qty from pending delta to actual position
            int32_t fill_delta =
                (o.side == Side::Buy) ? static_cast<int32_t>(fq) : -static_cast<int32_t>(fq);
            pending_position_delta_ -= fill_delta;

            update_position_on_fill(o.side, fq, report.fill_price);
            if (o.filled_qty == fq) filled_orders_++;
            fills_++;
            total_filled_qty_ += fq;
            if (o.side == Side::Buy) {
                buy_fills_++;
                buy_qty_ += fq;
            } else {
                sell_fills_++;
                sell_qty_ += fq;
            }

            if (fully_filled) {
                if (live_orders_ > 0) live_orders_--;
                free_slot(slot, o.id);
            }
            break;
        }

        case ExecType::CancelAck: {
            if (o.status == OrderStatus::PendingCancel) {
                if (pending_cxl_ > 0) pending_cxl_--;
            }
            if (o.type == OrderType::IOC && o.filled_qty == 0) {
                missed_ioc_++;
            }
            // Remove unfilled qty from pending position delta
            int32_t remaining = (o.side == Side::Buy) ? static_cast<int32_t>(o.leaves_qty)
                                                      : -static_cast<int32_t>(o.leaves_qty);
            pending_position_delta_ -= remaining;

            o.status         = OrderStatus::Cancelled;
            o.last_update_ts = report.timestamp_ns;
            if (live_orders_ > 0) live_orders_--;
            cancels_++;
            free_slot(slot, o.id);
            break;
        }

        case ExecType::Reject: {
            if (o.status == OrderStatus::Sent) {
                if (pending_new_ > 0) pending_new_--;
            }
            int32_t remaining = (o.side == Side::Buy) ? static_cast<int32_t>(o.leaves_qty)
                                                      : -static_cast<int32_t>(o.leaves_qty);
            pending_position_delta_ -= remaining;

            o.status         = OrderStatus::Rejected;
            o.last_update_ts = report.timestamp_ns;
            rejects_++;
            free_slot(slot, o.id);
            break;
        }

        case ExecType::CancelReject: {
            if (o.status == OrderStatus::PendingCancel) {
                o.status = (o.filled_qty > 0) ? OrderStatus::PartiallyFilled : OrderStatus::Live;
                if (pending_cxl_ > 0) pending_cxl_--;
            }
            o.last_update_ts = report.timestamp_ns;
            break;
        }

    }  // switch
}

// ── Position & PnL ──────────────────────────────────────────────────────────

void OMS::update_position_on_fill(Side side, Qty qty, Price fill_price) {
    int32_t delta   = (side == Side::Buy) ? static_cast<int32_t>(qty) : -static_cast<int32_t>(qty);
    int32_t old_pos = position_;
    int32_t new_pos = old_pos + delta;

    bool opening = (old_pos == 0) || (old_pos > 0 && delta > 0) || (old_pos < 0 && delta < 0);

    if (opening) {
        double old_cost  = avg_entry_ * std::abs(static_cast<double>(old_pos));
        double add_cost  = fill_price * static_cast<double>(qty);
        double new_total = std::abs(static_cast<double>(new_pos));
        avg_entry_       = (new_total > 0.0) ? (old_cost + add_cost) / new_total : 0.0;
    } else {
        Qty close_qty = static_cast<Qty>(std::min(static_cast<int32_t>(qty), std::abs(old_pos)));
        Qty open_qty  = qty - close_qty;

        double pnl_per_unit = (old_pos > 0) ? (fill_price - avg_entry_) : (avg_entry_ - fill_price);
        realized_pnl_ += pnl_per_unit * static_cast<double>(close_qty);

        if (open_qty > 0) {
            avg_entry_ = fill_price;
        } else if (new_pos == 0) {
            avg_entry_ = 0.0;
        }
    }

    position_ = new_pos;
}

double OMS::unrealized_pnl(Price current_mid) const {
    if (position_ == 0 || avg_entry_ == 0.0) return 0.0;
    return static_cast<double>(position_) * (current_mid - avg_entry_);
}

double OMS::total_pnl(Price current_mid) const {
    return realized_pnl_ + unrealized_pnl(current_mid);
}

}  // namespace nts
