#pragma once

#include "market_data.h"
#include <cstdint>

namespace nts {

struct OrderBook {
    double   bid_price      = 0.0;
    uint32_t bid_size       = 0;
    double   ask_price      = 0.0;
    uint32_t ask_size       = 0;
    uint64_t last_update_ts = 0;
    uint32_t update_count   = 0;

    void   update(const MdMsg& msg);
    double mid_price() const;
    double spread() const;
    double imbalance() const;
};

} // namespace nts
