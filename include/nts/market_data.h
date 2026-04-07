#pragma once

#include "common.h"
#include <cstdint>

namespace nts {

struct MdMsg {
    uint64_t timestamp_ns;
    uint32_t instrument_id;
    uint32_t sequence_num;
    double   bid_price;
    double   ask_price;
    uint32_t bid_size;
    uint32_t ask_size;
};

static_assert(sizeof(MdMsg) == 40, "MdMsg layout changed unexpectedly");

class MdReceiver {
public:
    bool init(uint16_t port);
    bool poll(MdMsg& msg);
    void close();

    uint64_t packets_received() const { return packets_; }
    uint64_t packets_dropped() const  { return drops_; }

private:
    int      sockfd_   = -1;
    uint64_t packets_  = 0;
    uint64_t drops_    = 0;
    uint32_t last_seq_ = 0;
};

} // namespace nts
