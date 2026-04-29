#pragma once

#include <cstdint>
#include "common.h"

namespace nts {

static constexpr int MD_MAX_DEPTH = 10;

enum class MdMsgType : uint8_t {
    Quote = 1,
    Reference = 4,
};

struct MdHeader {
    uint64_t  timestamp_ns;
    uint32_t  instrument_id;
    uint32_t  sequence_num;
    MdMsgType type;
    uint8_t   _pad[7];
};
static_assert(sizeof(MdHeader) == 24, "MdHeader layout mismatch");

struct MdQuote {
    MdHeader header;
    double   bid_price;
    double   ask_price;
    Qty      bid_size;
    Qty      ask_size;
};
static_assert(sizeof(MdQuote) == 48, "MdQuote layout mismatch");

struct MdReference {
    MdHeader header;
    double   reference_mid;
    uint8_t  _pad[8];
};
static_assert(sizeof(MdReference) == 40, "MdReference layout mismatch");

// Union for receiving any message type over UDP.
union MdMsg {
    MdHeader    header;
    MdQuote     quote;
    MdReference reference;
};

class MdReceiver {
public:
    static constexpr const char* DEFAULT_MULTICAST_GROUP = "239.1.1.1";

    bool init(uint16_t port, const char* multicast_group = DEFAULT_MULTICAST_GROUP);
    bool poll(MdMsg& msg);
    void close();

    uint64_t packets_received() const { return packets_; }
    uint64_t packets_dropped() const { return drops_; }
    uint64_t quotes_received() const { return quotes_; }
    uint64_t references_received() const { return references_; }

private:
    int      sockfd_   = -1;
    uint64_t packets_    = 0;
    uint64_t drops_      = 0;
    uint64_t quotes_     = 0;
    uint64_t references_ = 0;
    uint32_t last_seq_   = 0;
};

}  // namespace nts
