#pragma once

#include <cstdint>

#if defined(NTS_PERF_SYMBOLS)
#define NTS_NOINLINE __attribute__((noinline))
#else
#define NTS_NOINLINE
#endif

namespace nts {

using Price   = double;
using Qty     = uint32_t;
using OrderId = uint64_t;

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t {
    Limit,
    Market,
    IOC,
};

enum class OrderStatus : uint8_t {
    Empty,
    PendingNew,
    Sent,
    Live,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected,
};

enum class ExecType : uint8_t {
    NewAck,
    Fill,
    PartialFill,
    CancelAck,
    Reject,
    CancelReject,
};

constexpr uint16_t DEFAULT_PORT       = 12345;
constexpr uint32_t DEFAULT_INSTRUMENT = 1;
constexpr int32_t  MAX_POSITION       = 500;

}  // namespace nts
