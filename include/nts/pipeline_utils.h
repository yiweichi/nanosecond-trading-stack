#pragma once

#include "nts/instrument/clock.h"
#include "nts/instrument/tick_sampler.h"
#include "nts/order_gateway.h"
#include "nts/orderbook.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <string>

namespace nts {

struct Args {
    uint16_t    md_port       = nts::DEFAULT_PORT;
    const char* md_group      = nts::MdReceiver::DEFAULT_MULTICAST_GROUP;
    uint16_t    ref_port      = 12347;
    const char* ref_group     = "239.1.1.2";
    int         duration_sec  = 10;
    const char* exchange_host = "127.0.0.1";
    uint16_t    order_port    = nts::OrderGateway::DEFAULT_ORDER_PORT;
    bool        save_report   = false;
};

struct OrderTickRing {
    static constexpr size_t CAPACITY = nts::OMS::ORDER_MAP_SIZE;
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "OrderTickRing capacity must be power-of-two");

    void set(nts::OrderId id, uint64_t ticks);
    bool get(nts::OrderId id, uint64_t& ticks) const;
    void erase(nts::OrderId id);
    size_t collisions() const;

private:
    struct Entry {
        nts::OrderId id       = 0;
        uint64_t     ticks    = 0;
        bool         occupied = false;
    };

    static constexpr size_t index(nts::OrderId id) {
        return static_cast<size_t>(id) & (CAPACITY - 1);
    }

    std::array<Entry, CAPACITY> entries_ = {};
    size_t                      collisions_ = 0;
};

Args parse_args(int argc, char* argv[]);
void print_order_tick_ring_report(const char* name, const OrderTickRing& ring,
                                  FILE* out = stderr);
void print_trading_report(const nts::MdReceiver& ref_md, const nts::MdReceiver& target_md,
                          nts::OMS& oms, const nts::OrderBook& book, double elapsed_s,
                          uint64_t iterations, FILE* out = stderr);
const char* live_results_dir();
void mkdirs(const std::string& path);
std::string utc_timestamp();

}  // namespace nts
