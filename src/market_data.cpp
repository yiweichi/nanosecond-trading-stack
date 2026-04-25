#include "nts/market_data.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace nts {

bool MdReceiver::init(uint16_t port) {
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        perror("socket");
        return false;
    }

    int flags = fcntl(sockfd_, F_GETFL, 0);
    if (fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl O_NONBLOCK");
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    int reuse = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    fprintf(stderr, "[MdReceiver] listening on UDP port %u\n", port);
    return true;
}

bool MdReceiver::poll(MdMsg& msg) {
    if (sockfd_ < 0) return false;

    ssize_t n = recvfrom(sockfd_, &msg, sizeof(MdMsg), 0, nullptr, nullptr);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
        perror("recvfrom");
        return false;
    }

    if (n < static_cast<ssize_t>(sizeof(MdHeader))) return false;

    bool valid = false;
    switch (msg.header.type) {
        case MdMsgType::Quote:
            valid = (n == static_cast<ssize_t>(sizeof(MdQuote)));
            if (valid) quotes_++;
            break;
        case MdMsgType::Reference:
            valid = (n == static_cast<ssize_t>(sizeof(MdReference)));
            if (valid) references_++;
            break;
        case MdMsgType::Depth:
            valid = (n == static_cast<ssize_t>(sizeof(MdDepth)));
            if (valid) depths_++;
            break;
        case MdMsgType::Trade:
            valid = (n == static_cast<ssize_t>(sizeof(MdTrade)));
            if (valid) trades_++;
            break;
        default: return false;
    }
    if (!valid) return false;

    uint32_t seq = msg.header.sequence_num;
    if (packets_ > 0 && seq != last_seq_ + 1) {
        drops_ += (seq - last_seq_ - 1);
    }
    last_seq_ = seq;
    packets_++;

    return true;
}

void MdReceiver::close() {
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

}  // namespace nts
