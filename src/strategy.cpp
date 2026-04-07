#include "nts/strategy.h"

namespace nts {

ImbalanceStrategy::ImbalanceStrategy(const StrategyParams& params) : params_(params) {}

Signal ImbalanceStrategy::on_book_update(const OrderBook& book) {
    if (!book.valid()) return Signal::None;

    double imb = book.imbalance(params_.imbalance_levels);

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

}  // namespace nts
