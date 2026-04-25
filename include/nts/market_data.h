#pragma once

#include <cstdint>
#include "common.h"

namespace nts {

static constexpr int MD_MAX_DEPTH = 10;

enum class MdMsgType : uint8_t {
    Quote = 1,
    Depth = 2,
    Trade = 3,
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

struct MdDepthLevel {
    double price;
    Qty    size;
    Qty    order_count;
};
static_assert(sizeof(MdDepthLevel) == 16, "MdDepthLevel layout mismatch");

struct MdDepth {
    MdHeader     header;
    uint8_t      bid_levels;
    uint8_t      ask_levels;
    uint8_t      _pad[6];
    MdDepthLevel bids[MD_MAX_DEPTH];
    MdDepthLevel asks[MD_MAX_DEPTH];
};

struct MdTrade {
    MdHeader header;
    double   price;
    Qty      size;
    Side     aggressor_side;
    uint8_t  _pad[3];
};

// Union for receiving any message type over UDP.
// Size equals the largest member (MdDepth).
union MdMsg {
    MdHeader    header;
    MdQuote     quote;
    MdReference reference;
    MdDepth     depth;
    MdTrade     trade;
};

class MdReceiver {
public:
    bool init(uint16_t port);
    bool poll(MdMsg& msg);
    void close();

    uint64_t packets_received() const { return packets_; }
    uint64_t packets_dropped() const { return drops_; }
    uint64_t quotes_received() const { return quotes_; }
    uint64_t references_received() const { return references_; }
    uint64_t depths_received() const { return depths_; }
    uint64_t trades_received() const { return trades_; }

private:
    int      sockfd_   = -1;
    uint64_t packets_    = 0;
    uint64_t drops_      = 0;
    uint64_t quotes_     = 0;
    uint64_t references_ = 0;
    uint64_t depths_     = 0;
    uint64_t trades_     = 0;
    uint32_t last_seq_   = 0;
};

}  // namespace nts
