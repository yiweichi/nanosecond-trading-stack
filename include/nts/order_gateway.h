#pragma once

#include <cstddef>
#include <cstdint>
#include "oms.h"
#include "wire_protocol.h"

namespace nts {

/// TCP client that connects to the Rust exchange server.
class OrderGateway {
public:
    static constexpr uint16_t DEFAULT_ORDER_PORT = wire::DEFAULT_ORDER_PORT;

    bool connect(const char* host, uint16_t port);
    void submit_order(const Order& order);
    bool poll_execution(ExecutionReport& report);
    void close();

    bool connected() const { return sockfd_ >= 0; }

private:
    int sockfd_ = -1;

    static constexpr size_t BUF_SIZE = 4096;
    uint8_t read_buf_[BUF_SIZE] = {};
    size_t  read_len_            = 0;
};

}  // namespace nts
