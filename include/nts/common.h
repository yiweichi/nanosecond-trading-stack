#pragma once

#include <cstdint>

namespace nts {

using Timestamp = uint64_t;

enum class Side : uint8_t { Buy, Sell };

inline const char* side_to_str(Side s) {
    return s == Side::Buy ? "BUY" : "SELL";
}

enum class OrderStatus : uint8_t {
    Empty,
    Pending,
    Filled,
    Rejected
};

constexpr uint16_t DEFAULT_PORT        = 12345;
constexpr uint32_t DEFAULT_INSTRUMENT  = 1;
constexpr int32_t  MAX_POSITION        = 500;
constexpr uint32_t DEFAULT_ORDER_SIZE  = 100;

} // namespace nts
