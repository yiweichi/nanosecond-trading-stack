#include "nts/strategy.h"

namespace nts {

ImbalanceStrategy::ImbalanceStrategy(const StrategyParams& params)
    : params_(params)
{}

Signal ImbalanceStrategy::on_book_update(const OrderBook& book) {
    double imb = book.imbalance();

    if (imb > params_.imbalance_threshold) {
        signals_++;
        return Signal::Buy;
    }
    if (imb < -params_.imbalance_threshold) {
        signals_++;
        return Signal::Sell;
    }

    return Signal::None;
}

} // namespace nts
