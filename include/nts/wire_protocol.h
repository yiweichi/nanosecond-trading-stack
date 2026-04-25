#pragma once

#include <cstdint>
#include <cstring>

namespace nts {
namespace wire {

// ── Constants (must match Rust matching-engine/src/server/wire.rs) ────────────

constexpr uint8_t  ORDER_MSG_NEW      = 1;
constexpr uint8_t  ORDER_MSG_CANCEL   = 2;

constexpr uint8_t ORDER_TYPE_LIMIT     = 0;
constexpr uint8_t ORDER_TYPE_MARKET    = 1;
constexpr uint8_t ORDER_TYPE_IOC_LIMIT = 2;

constexpr uint16_t DEFAULT_MD_PORT    = 12345;
constexpr uint16_t DEFAULT_ORDER_PORT = 12346;
constexpr uint32_t DEFAULT_INSTRUMENT = 1;

// ── Order message: C++ HFT -> Rust Exchange (via TCP) ────────────────────────

struct WireOrderMsg {
    uint8_t  msg_type;          // ORDER_MSG_NEW or ORDER_MSG_CANCEL
    uint8_t  side;              // 0 = Buy, 1 = Sell
    uint8_t  order_type;        // 0 = Limit, 1 = Market, 2 = IOC limit
    uint8_t  _pad1[5];
    uint64_t client_order_id;
    double   price;
    uint32_t qty;
    uint32_t _pad2;
    uint64_t cancel_order_id;   // only for ORDER_MSG_CANCEL
};
static_assert(sizeof(WireOrderMsg) == 40, "WireOrderMsg layout mismatch");

// ── Execution report: Rust Exchange -> C++ HFT (via TCP) ─────────────────────

struct WireExecReport {
    uint8_t  exec_type;         // matches nts::ExecType values (0-5)
    uint8_t  side;              // 0 = Buy, 1 = Sell
    uint8_t  _pad1[2];
    uint32_t fill_qty;
    uint64_t order_id;
    double   fill_price;
    uint32_t leaves_qty;
    uint32_t _pad2;
    uint64_t timestamp_ns;
};
static_assert(sizeof(WireExecReport) == 40, "WireExecReport layout mismatch");

}  // namespace wire
}  // namespace nts
