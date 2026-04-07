#pragma once

#include "orderbook.h"
#include <cstdint>

namespace nts {

enum class Signal : uint8_t {
    None,
    Buy,
    Sell
};

struct StrategyParams {
    double   imbalance_threshold = 0.3;
    uint32_t order_size          = DEFAULT_ORDER_SIZE;
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

} // namespace nts
