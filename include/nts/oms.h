#pragma once

#include <cstddef>
#include <cstdint>
#include "common.h"

namespace nts {

struct Order {
    OrderId     id             = 0;
    Price       price          = 0.0;
    Qty         qty            = 0;
    Qty         filled_qty     = 0;
    Qty         leaves_qty     = 0;
    Side        side           = Side::Buy;
    OrderType   type           = OrderType::Limit;
    OrderStatus status         = OrderStatus::Empty;
    uint64_t    create_ts      = 0;
    uint64_t    sent_ts        = 0;
    uint64_t    last_update_ts = 0;
    double      avg_fill_price = 0.0;
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
    Order* send_new(Side side, Price price, Qty qty, OrderType type = OrderType::Limit);
    bool   send_cancel(OrderId order_id);

    // ── Execution processing ─────────────────────────────────────
    void on_execution(const ExecutionReport& report);

    // ── Risk ─────────────────────────────────────────────────────
    void set_risk_limits(const RiskLimits& limits) { risk_ = limits; }
    void set_reference_price(Price price) { ref_price_ = price; }
    bool check_risk(Side side, Price price, Qty qty) const;

    // ── Order queries ────────────────────────────────────────────
    const Order* find_order(OrderId id) const;
    Order*       find_order(OrderId id);

    // ── Position & PnL ───────────────────────────────────────────
    int32_t net_position() const { return position_; }
    double  avg_entry_price() const { return avg_entry_; }
    double  realized_pnl() const { return realized_pnl_; }
    double  unrealized_pnl(Price current_mid) const;
    double  total_pnl(Price current_mid) const;

    // ── Statistics ───────────────────────────────────────────────
    size_t live_order_count() const { return live_orders_; }
    size_t pending_count() const { return pending_new_ + pending_cxl_; }
    size_t total_orders() const { return order_count_; }
    size_t total_fills() const { return fills_; }
    size_t total_cancels() const { return cancels_; }
    size_t total_rejects() const { return rejects_; }
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
    double  avg_entry_              = 0.0;
    double  realized_pnl_           = 0.0;

    // Risk
    RiskLimits risk_;
    Price      ref_price_   = 0.0;
    size_t     live_orders_ = 0;
    size_t     pending_new_ = 0;
    size_t     pending_cxl_ = 0;

    // Stats
    size_t fills_            = 0;
    size_t cancels_          = 0;
    size_t rejects_          = 0;
    Qty    total_filled_qty_ = 0;

    // Hash map helpers (Fibonacci hashing + linear probing)
    size_t map_hash(OrderId id) const;
    size_t map_find(OrderId id) const;
    void   map_insert(OrderId id, uint32_t slot);
    void   map_remove(OrderId id);

    size_t alloc_slot();
    void   free_slot(size_t slot, OrderId id);
    void   update_position_on_fill(Side side, Qty qty, Price price);
};

}  // namespace nts
