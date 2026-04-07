#pragma once

#include <cstdint>
#include "orderbook.h"

namespace nts {

enum class Signal : uint8_t { None, Buy, Sell };

struct StrategyParams {
    double imbalance_threshold = 0.3;
    Qty    order_size          = DEFAULT_ORDER_SIZE;
    int    imbalance_levels    = 1;
};

class ImbalanceStrategy {
public:
    explicit ImbalanceStrategy(const StrategyParams& params);

    Signal on_book_update(const OrderBook& book);

    uint64_t signals_generated() const { return signals_; }

private:
    StrategyParams params_;
    uint64_t       signals_ = 0;
};

}  // namespace nts
