#include "nts/market_data.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace nts {

bool MdReceiver::init(uint16_t port, const char* multicast_group) {
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
#ifdef SO_REUSEPORT
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

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

    struct ip_mreq mreq;
    std::memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(multicast_group);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (mreq.imr_multiaddr.s_addr == INADDR_NONE ||
        setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("IP_ADD_MEMBERSHIP");
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    fprintf(stderr, "[MdReceiver] listening on UDP multicast %s:%u\n", multicast_group, port);
    return true;
}

bool MdReceiver::poll(MdMsg& msg) {
    if (sockfd_ < 0) return false;

    bool  got_latest = false;
    MdMsg latest;
    std::memset(&latest, 0, sizeof(latest));

    while (true) {
        MdMsg candidate;
        std::memset(&candidate, 0, sizeof(candidate));
        ssize_t n = recvfrom(sockfd_, &candidate, sizeof(candidate), 0, nullptr, nullptr);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("recvfrom");
            return got_latest;
        }

        if (n < static_cast<ssize_t>(sizeof(MdHeader))) continue;

        bool valid = false;
        switch (candidate.header.type) {
            case MdMsgType::Quote:
                valid = (n == static_cast<ssize_t>(sizeof(MdQuote)));
                if (valid) quotes_++;
                break;
            case MdMsgType::Reference:
                valid = (n == static_cast<ssize_t>(sizeof(MdReference)));
                if (valid) references_++;
                break;
            default: continue;
        }
        if (!valid) continue;

        uint32_t seq = candidate.header.sequence_num;
        if (packets_ > 0 && seq != last_seq_ + 1) {
            drops_ += (seq - last_seq_ - 1);
        }
        last_seq_ = seq;
        packets_++;

        latest     = candidate;
        got_latest = true;
    }

    if (got_latest) {
        msg = latest;
    }
    return got_latest;
}

void MdReceiver::close() {
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

}  // namespace nts
