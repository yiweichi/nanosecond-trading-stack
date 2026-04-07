#include "nts/exchange.h"
#include "nts/instrument/clock.h"

namespace nts {

float MockExchange::fast_rand() {
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    return static_cast<float>(rng_state_) / static_cast<float>(UINT32_MAX);
}

void MockExchange::submit_order(const Order& order) {
    uint64_t now = instrument::now_ns();
    for (auto& s : pending_) {
        if (!s.active) {
            s.order         = order;
            s.type          = PendingType::NewOrder;
            s.ack_ready_ns  = now + config_.ack_latency_ns;
            s.fill_ready_ns = now + config_.fill_latency_ns;
            s.active        = true;
            s.acked         = false;
            return;
        }
    }
}

void MockExchange::submit_cancel(OrderId order_id) {
    uint64_t now = instrument::now_ns();
    for (auto& s : pending_) {
        if (!s.active) {
            s.cancel_target = order_id;
            s.type          = PendingType::Cancel;
            s.ack_ready_ns  = now + config_.cancel_latency_ns;
            s.active        = true;
            s.acked         = false;
            return;
        }
    }
}

bool MockExchange::poll_execution(ExecutionReport& report) {
    uint64_t now = instrument::now_ns();

    for (auto& s : pending_) {
        if (!s.active || now < s.ack_ready_ns) continue;

        // ── Cancel path ──────────────────────────────────────────
        if (s.type == PendingType::Cancel) {
            report.order_id     = s.cancel_target;
            report.exec_type    = ExecType::CancelAck;
            report.fill_price   = 0.0;
            report.fill_qty     = 0;
            report.leaves_qty   = 0;
            report.timestamp_ns = now;
            s.active            = false;
            return true;
        }

        // ── NewOrder: send ack or reject ─────────────────────────
        if (!s.acked) {
            if (config_.reject_probability > 0.0 &&
                fast_rand() < static_cast<float>(config_.reject_probability)) {
                report.order_id     = s.order.id;
                report.exec_type    = ExecType::Reject;
                report.fill_price   = 0.0;
                report.fill_qty     = 0;
                report.leaves_qty   = s.order.qty;
                report.timestamp_ns = now;
                s.active            = false;
                return true;
            }

            report.order_id     = s.order.id;
            report.exec_type    = ExecType::NewAck;
            report.fill_price   = 0.0;
            report.fill_qty     = 0;
            report.leaves_qty   = s.order.qty;
            report.timestamp_ns = now;
            s.acked             = true;
            return true;
        }

        // ── NewOrder: fill when ready ────────────────────────────
        if (now < s.fill_ready_ns) continue;

        if (config_.fill_probability < 1.0 &&
            fast_rand() >= static_cast<float>(config_.fill_probability)) {
            s.active = false;
            continue;
        }

        Qty fill_qty = s.order.qty;
        if (config_.partial_fill_ratio > 0.0 && fast_rand() < 0.5f) {
            fill_qty = static_cast<Qty>(static_cast<double>(fill_qty) * config_.partial_fill_ratio);
            if (fill_qty == 0) fill_qty = 1;
        }

        bool full = (fill_qty >= s.order.qty);

        report.order_id     = s.order.id;
        report.exec_type    = full ? ExecType::Fill : ExecType::PartialFill;
        report.fill_price   = s.order.price;
        report.fill_qty     = fill_qty;
        report.leaves_qty   = full ? 0 : (s.order.qty - fill_qty);
        report.timestamp_ns = now;

        if (full) {
            s.active = false;
        } else {
            s.order.qty -= fill_qty;
            s.fill_ready_ns = now + config_.fill_latency_ns;
        }

        return true;
    }

    return false;
}

size_t MockExchange::pending_orders() const {
    size_t n = 0;
    for (const auto& s : pending_) {
        if (s.active) n++;
    }
    return n;
}

}  // namespace nts
