#pragma once

#include "oms.h"
#include <cstddef>
#include <cstdint>

namespace nts {

// In-process mock exchange. Simulates fill latency via timestamp comparison
// (never sleeps). Submit orders and poll for acks.
class MockExchange {
public:
    static constexpr size_t   MAX_PENDING       = 256;
    static constexpr uint64_t DEFAULT_LATENCY_NS = 5'000; // 5 microseconds

    void submit_order(const Order& order);
    bool poll_ack(Ack& ack);

    void     set_latency_ns(uint64_t ns) { latency_ns_ = ns; }
    uint64_t latency_ns() const          { return latency_ns_; }

private:
    struct PendingOrder {
        Order    order;
        uint64_t ready_at_ns = 0;
        bool     active      = false;
    };

    PendingOrder pending_[MAX_PENDING] = {};
    uint64_t     latency_ns_ = DEFAULT_LATENCY_NS;
};

} // namespace nts
