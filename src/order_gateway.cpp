#include "nts/order_gateway.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace nts {

bool OrderGateway::connect(const char* host, uint16_t port) {
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
        perror("OrderGateway socket");
        return false;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);

    if (::connect(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("OrderGateway connect");
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    // Non-blocking for recv
    int flags = fcntl(sockfd_, F_GETFL, 0);
    fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

    // Disable Nagle for low latency
    int one = 1;
    setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    fprintf(stderr, "[OrderGateway] connected to %s:%u\n", host, port);
    return true;
}

void OrderGateway::submit_order(const Order& order) {
    if (sockfd_ < 0) return;

    wire::WireOrderMsg msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_type = wire::ORDER_MSG_NEW;
    msg.side     = static_cast<uint8_t>(order.side);
    switch (order.type) {
        case OrderType::Limit: msg.order_type = wire::ORDER_TYPE_LIMIT; break;
        case OrderType::Market: msg.order_type = wire::ORDER_TYPE_MARKET; break;
        case OrderType::IOC: msg.order_type = wire::ORDER_TYPE_IOC_LIMIT; break;
    }
    msg.client_order_id = order.id;
    msg.price           = order.price;
    msg.qty             = order.qty;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(&msg);
    size_t         left = sizeof(msg);
    while (left > 0) {
        ssize_t n = ::send(sockfd_, data, left, MSG_NOSIGNAL);
        if (n > 0) {
            data += n;
            left -= static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        perror("OrderGateway send order");
        close();
        return;
    }
}

void OrderGateway::submit_cancel(OrderId order_id) {
    if (sockfd_ < 0) return;

    wire::WireOrderMsg msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_type        = wire::ORDER_MSG_CANCEL;
    msg.cancel_order_id = order_id;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(&msg);
    size_t         left = sizeof(msg);
    while (left > 0) {
        ssize_t n = ::send(sockfd_, data, left, MSG_NOSIGNAL);
        if (n > 0) {
            data += n;
            left -= static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        perror("OrderGateway send cancel");
        close();
        return;
    }
}

bool OrderGateway::poll_execution(ExecutionReport& report) {
    if (sockfd_ < 0) return false;

    while (read_len_ < BUF_SIZE) {
        ssize_t n = recv(sockfd_, read_buf_ + read_len_, BUF_SIZE - read_len_, 0);
        if (n > 0) {
            read_len_ += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            fprintf(stderr, "[OrderGateway] exchange disconnected\n");
            close();
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        perror("OrderGateway recv execution");
        close();
        return false;
    }

    if (read_len_ < sizeof(wire::WireExecReport)) return false;

    wire::WireExecReport wire_rpt;
    std::memcpy(&wire_rpt, read_buf_, sizeof(wire_rpt));

    report.order_id     = wire_rpt.order_id;
    report.exec_type    = static_cast<ExecType>(wire_rpt.exec_type);
    report.fill_price   = wire_rpt.fill_price;
    report.fill_qty     = wire_rpt.fill_qty;
    report.leaves_qty   = wire_rpt.leaves_qty;
    report.timestamp_ns = wire_rpt.timestamp_ns;

    // Shift remaining bytes forward
    size_t consumed = sizeof(wire::WireExecReport);
    read_len_ -= consumed;
    if (read_len_ > 0) {
        std::memmove(read_buf_, read_buf_ + consumed, read_len_);
    }

    return true;
}

void OrderGateway::close() {
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
    read_len_ = 0;
}

}  // namespace nts
