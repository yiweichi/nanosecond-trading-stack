#include "nts/orderbook.h"

namespace nts {

void OrderBook::update(const MdMsg& msg) {
    bid_price      = msg.bid_price;
    bid_size       = msg.bid_size;
    ask_price      = msg.ask_price;
    ask_size       = msg.ask_size;
    last_update_ts = msg.timestamp_ns;
    update_count++;
}

double OrderBook::mid_price() const {
    return (bid_price + ask_price) * 0.5;
}

double OrderBook::spread() const {
    return ask_price - bid_price;
}

double OrderBook::imbalance() const {
    double total = static_cast<double>(bid_size) + static_cast<double>(ask_size);
    if (total == 0.0) return 0.0;
    return (static_cast<double>(bid_size) - static_cast<double>(ask_size)) / total;
}

} // namespace nts
