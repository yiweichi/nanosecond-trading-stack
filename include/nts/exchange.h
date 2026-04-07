#pragma once

#include <cstddef>
#include <cstdint>
#include "oms.h"

namespace nts {

class MockExchange {
public:
    static constexpr size_t   MAX_PENDING        = 256;
    static constexpr uint64_t DEFAULT_LATENCY_NS = 5'000;

    struct Config {
        uint64_t ack_latency_ns     = DEFAULT_LATENCY_NS;
        uint64_t fill_latency_ns    = DEFAULT_LATENCY_NS;
        uint64_t cancel_latency_ns  = 3'000;
        double   fill_probability   = 1.0;
        double   partial_fill_ratio = 0.0;
        double   reject_probability = 0.0;
    };

    void configure(const Config& cfg) { config_ = cfg; }

    void submit_order(const Order& order);
    void submit_cancel(OrderId order_id);
    bool poll_execution(ExecutionReport& report);

    size_t pending_orders() const;

private:
    enum class PendingType : uint8_t { NewOrder, Cancel };

    struct PendingEntry {
        Order       order;
        OrderId     cancel_target = 0;
        PendingType type          = PendingType::NewOrder;
        uint64_t    ack_ready_ns  = 0;
        uint64_t    fill_ready_ns = 0;
        bool        active        = false;
        bool        acked         = false;
    };

    PendingEntry pending_[MAX_PENDING] = {};
    Config       config_;
    uint32_t     rng_state_ = 12345;

    float fast_rand();
};

}  // namespace nts
