#pragma once

#include <cstdint>

namespace nts {

using Timestamp = uint64_t;
using Price     = double;
using Qty       = uint32_t;
using OrderId   = uint64_t;

enum class Side : uint8_t { Buy, Sell };

inline const char* side_to_str(Side s) {
    return s == Side::Buy ? "BUY" : "SELL";
}

enum class OrderType : uint8_t {
    Limit,
    Market,
    IOC,
};

inline const char* order_type_to_str(OrderType t) {
    switch (t) {
        case OrderType::Limit: return "LIMIT";
        case OrderType::Market: return "MARKET";
        case OrderType::IOC: return "IOC";
    }
    return "UNKNOWN";
}

enum class OrderStatus : uint8_t {
    Empty,
    PendingNew,
    Sent,
    Live,
    PartiallyFilled,
    Filled,
    PendingCancel,
    Cancelled,
    Rejected,
};

inline const char* order_status_to_str(OrderStatus s) {
    switch (s) {
        case OrderStatus::Empty: return "EMPTY";
        case OrderStatus::PendingNew: return "PENDING_NEW";
        case OrderStatus::Sent: return "SENT";
        case OrderStatus::Live: return "LIVE";
        case OrderStatus::PartiallyFilled: return "PARTIAL_FILL";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::PendingCancel: return "PENDING_CXL";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::Rejected: return "REJECTED";
    }
    return "UNKNOWN";
}

enum class ExecType : uint8_t {
    NewAck,
    Fill,
    PartialFill,
    CancelAck,
    Reject,
    CancelReject,
};

inline const char* exec_type_to_str(ExecType e) {
    switch (e) {
        case ExecType::NewAck: return "NEW_ACK";
        case ExecType::Fill: return "FILL";
        case ExecType::PartialFill: return "PARTIAL_FILL";
        case ExecType::CancelAck: return "CANCEL_ACK";
        case ExecType::Reject: return "REJECT";
        case ExecType::CancelReject: return "CANCEL_REJECT";
    }
    return "UNKNOWN";
}

constexpr uint16_t DEFAULT_PORT       = 12345;
constexpr uint32_t DEFAULT_INSTRUMENT = 1;
constexpr int32_t  MAX_POSITION       = 500;
constexpr Qty      DEFAULT_ORDER_SIZE = 100;

}  // namespace nts
