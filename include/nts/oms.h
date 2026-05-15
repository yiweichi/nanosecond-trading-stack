#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include "common.h"

namespace nts {

struct Order {
    OrderId     id                   = 0;
    Price       price                = 0.0;
    Qty         qty                  = 0;
    Qty         filled_qty           = 0;
    Qty         leaves_qty           = 0;
    Side        side                 = Side::Buy;
    OrderType   type                 = OrderType::Limit;
    OrderStatus status               = OrderStatus::Empty;
    uint64_t    source_exchange_tick = 0;
    uint64_t    client_reaction_ns   = 0;
    double      avg_fill_price       = 0.0;
};

struct ExecutionReport {
    OrderId  order_id     = 0;
    ExecType exec_type    = ExecType::Reject;
    Price    fill_price   = 0.0;
    Qty      fill_qty     = 0;
    Qty      leaves_qty   = 0;
    uint64_t timestamp_ns = 0;
};

struct RiskLimits {
    int32_t  max_position        = MAX_POSITION;
    Qty      max_order_qty       = 10000;
    Price    max_price_deviation = 5.0;
    uint32_t max_live_orders     = 50;
};

class OMS {
public:
    static constexpr size_t MAX_ORDERS     = 4096;
    static constexpr size_t ORDER_MAP_SIZE = 8192;

    // ── Order management ─────────────────────────────────────────
    NTS_ALWAYS_INLINE Order* send_new(Side side, Price price, Qty qty,
                                      OrderType type = OrderType::Limit);

    // ── Execution processing ─────────────────────────────────────
    void on_execution(const ExecutionReport& report);

    // ── Risk ─────────────────────────────────────────────────────
    void set_reference_price(Price price) { ref_price_ = price; }
    NTS_ALWAYS_INLINE bool check_risk(Side side, Price price, Qty qty) const;

    // ── Position & PnL ───────────────────────────────────────────
    int32_t net_position() const { return position_; }
    double  trading_pnl() const { return trading_cash_; }
    double  liquidation_pnl() const { return 0.0; }
    double  mark_pnl(Price current_mid) const;
    double  total_pnl(Price current_mid) const;

    // ── Statistics ───────────────────────────────────────────────
    size_t pending_count() const { return pending_new_; }
    size_t total_orders() const { return order_count_; }
    size_t total_accepted_orders() const { return accepted_orders_; }
    size_t total_filled_orders() const { return filled_orders_; }
    size_t total_missed_ioc() const { return missed_ioc_; }
    size_t total_fills() const { return fills_; }
    size_t total_buy_fills() const { return buy_fills_; }
    size_t total_sell_fills() const { return sell_fills_; }
    Qty    total_buy_qty() const { return buy_qty_; }
    Qty    total_sell_qty() const { return sell_qty_; }
    size_t total_cancels() const { return cancels_; }
    size_t total_rejects() const { return rejects_; }
    size_t total_failed_orders() const { return rejects_ + missed_ioc_; }
    Qty    total_filled_qty() const { return total_filled_qty_; }

private:
    // Order storage
    Order   orders_[MAX_ORDERS] = {};
    OrderId next_id_            = 1;
    size_t  order_count_        = 0;

    // Slot management: free stack for O(1) reuse of terminal-state slots
    size_t free_stack_[MAX_ORDERS] = {};
    size_t free_top_               = 0;
    size_t next_fresh_slot_        = 0;

    // Open-addressing hash map: order_id -> index in orders_
    struct MapEntry {
        OrderId  id       = 0;
        uint32_t slot     = 0;
        bool     occupied = false;
    };
    MapEntry order_map_[ORDER_MAP_SIZE] = {};

    // Position
    int32_t position_               = 0;
    int32_t pending_position_delta_ = 0;  // net qty of in-flight (unfilled) orders
    double  trading_cash_           = 0.0;

    // Risk
    RiskLimits risk_;
    Price      ref_price_   = 0.0;
    size_t     live_orders_ = 0;
    size_t     pending_new_ = 0;

    // Stats
    size_t accepted_orders_  = 0;
    size_t filled_orders_    = 0;
    size_t missed_ioc_       = 0;
    size_t fills_            = 0;
    size_t buy_fills_        = 0;
    size_t sell_fills_       = 0;
    size_t cancels_          = 0;
    size_t rejects_          = 0;
    Qty    total_filled_qty_ = 0;
    Qty    buy_qty_          = 0;
    Qty    sell_qty_         = 0;

    // Hash map helpers (Fibonacci hashing + linear probing)
    NTS_ALWAYS_INLINE size_t map_hash(OrderId id) const;
    size_t map_find(OrderId id) const;
    NTS_ALWAYS_INLINE void map_insert(OrderId id, uint32_t slot);
    void   map_remove(OrderId id);

    NTS_ALWAYS_INLINE size_t alloc_slot();
    void   free_slot(size_t slot, OrderId id);
    void   apply_trading_fill(Side side, Qty qty, Price price);
};

NTS_ALWAYS_INLINE size_t OMS::map_hash(OrderId id) const {
    return static_cast<size_t>((id * 11400714819323198485ULL) >> (64 - 13));
}

NTS_ALWAYS_INLINE void OMS::map_insert(OrderId id, uint32_t slot) {
    size_t idx = map_hash(id);
    for (size_t i = 0; i < ORDER_MAP_SIZE; i++) {
        size_t pos = (idx + i) & (ORDER_MAP_SIZE - 1);
        if (!order_map_[pos].occupied) {
            order_map_[pos] = {id, slot, true};
            return;
        }
    }
}

NTS_ALWAYS_INLINE size_t OMS::alloc_slot() {
    if (free_top_ > 0) return free_stack_[--free_top_];
    if (next_fresh_slot_ < MAX_ORDERS) return next_fresh_slot_++;
    return MAX_ORDERS;
}

NTS_ALWAYS_INLINE bool OMS::check_risk(Side side, Price price, Qty qty) const {
    if (qty > risk_.max_order_qty) return false;

    if (live_orders_ + pending_new_ >= risk_.max_live_orders) return false;

    int32_t base = position_ + pending_position_delta_;
    base += (side == Side::Buy) ? static_cast<int32_t>(qty) : -static_cast<int32_t>(qty);
    if (std::abs(base) > risk_.max_position) return false;

    if (ref_price_ > 0.0 && risk_.max_price_deviation > 0.0) {
        if (std::abs(price - ref_price_) > risk_.max_price_deviation) return false;
    }

    return true;
}

NTS_ALWAYS_INLINE Order* OMS::send_new(Side side, Price price, Qty qty, OrderType type) {
    if (!check_risk(side, price, qty)) return nullptr;

    size_t slot = alloc_slot();
    if (slot >= MAX_ORDERS) return nullptr;

    OrderId id = next_id_++;

    Order& o         = orders_[slot];
    o.id             = id;
    o.price          = price;
    o.qty            = qty;
    o.filled_qty     = 0;
    o.leaves_qty     = qty;
    o.side           = side;
    o.type           = type;
    o.status         = OrderStatus::Sent;
    o.avg_fill_price = 0.0;

    map_insert(id, static_cast<uint32_t>(slot));
    order_count_++;
    pending_new_++;
    pending_position_delta_ +=
        (side == Side::Buy) ? static_cast<int32_t>(qty) : -static_cast<int32_t>(qty);

    return &o;
}

}  // namespace nts
