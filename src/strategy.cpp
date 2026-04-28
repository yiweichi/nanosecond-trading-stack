#include "nts/strategy.h"
namespace nts {

ImbalanceStrategy::ImbalanceStrategy(const StrategyParams& params) : params_(params) {}

Signal ImbalanceStrategy::on_book_update(const OrderBook& book, int32_t position, uint32_t seq) {
    if (!book.valid() || !book.has_reference()) return Signal::None;

    const Price reference = book.reference_mid();

    if (reference >= book.best_ask() + params_.edge_threshold &&
        position < params_.max_position) {
        signals_++;
        return Signal::Buy;
    }

    if (book.best_bid() >= reference + params_.edge_threshold &&
        position > -params_.max_position) {
        signals_++;
        return Signal::Sell;
    }

    return Signal::None;
}

}  // namespace nts
