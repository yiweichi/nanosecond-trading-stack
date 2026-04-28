#pragma once

#include <cstdint>
#include "orderbook.h"

namespace nts {

enum class Signal : uint8_t { None, Buy, Sell };

struct StrategyParams {
    Price edge_threshold = 2.0;
    Qty   order_size     = 1;
    int   max_position   = MAX_POSITION;
};

class ImbalanceStrategy {
public:
    explicit ImbalanceStrategy(const StrategyParams& params);

    Signal on_book_update(const OrderBook& book, int32_t position);

    uint64_t signals_generated() const { return signals_; }
    Qty      order_size() const { return params_.order_size; }

private:
    StrategyParams params_;
    uint64_t       signals_             = 0;
};

}  // namespace nts
