#include "nts/matching_engine.h"
#include <cassert>
#include <cstdio>

using namespace nts::matching;

static Order limit(uint64_t id, Side side, Price price, Qty qty) {
    return {id, side, price, qty, OrderType::Limit};
}

static Order market(uint64_t id, Side side, Qty qty) {
    return {id, side, 0, qty, OrderType::Market};
}

static void test_no_match_wide_spread() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Buy, 100, 10), fills);
    assert(fills.empty());
    book.add_order(limit(2, Side::Sell, 110, 10), fills);
    assert(fills.empty());

    assert(book.best_bid() == 100);
    assert(book.best_ask() == 110);
    assert(book.spread() == 10);
    assert(book.len() == 2);
    fprintf(stderr, "  PASS: no_match_wide_spread\n");
}

static void test_exact_fill() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Sell, 100, 10), fills);
    assert(fills.empty());

    book.add_order(limit(2, Side::Buy, 100, 10), fills);
    assert(fills.size() == 1);
    assert(fills[0].maker_id == 1);
    assert(fills[0].taker_id == 2);
    assert(fills[0].price == 100);
    assert(fills[0].qty == 10);
    assert(book.is_empty());
    fprintf(stderr, "  PASS: exact_fill\n");
}

static void test_partial_fill() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Sell, 100, 20), fills);
    fills.clear();

    book.add_order(limit(2, Side::Buy, 100, 5), fills);
    assert(fills.size() == 1);
    assert(fills[0].qty == 5);
    assert(book.depth_at(Side::Sell, 100) == 15);
    assert(book.len() == 1);
    fprintf(stderr, "  PASS: partial_fill\n");
}

static void test_price_improvement() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Sell, 95, 10), fills);
    fills.clear();

    book.add_order(limit(2, Side::Buy, 100, 10), fills);
    assert(fills.size() == 1);
    assert(fills[0].price == 95);
    fprintf(stderr, "  PASS: price_improvement\n");
}

static void test_fifo_priority() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Sell, 100, 5), fills);
    book.add_order(limit(2, Side::Sell, 100, 5), fills);
    fills.clear();

    book.add_order(limit(3, Side::Buy, 100, 7), fills);
    assert(fills.size() == 2);
    assert(fills[0].maker_id == 1);
    assert(fills[0].qty == 5);
    assert(fills[1].maker_id == 2);
    assert(fills[1].qty == 2);
    assert(book.depth_at(Side::Sell, 100) == 3);
    fprintf(stderr, "  PASS: fifo_priority\n");
}

static void test_multi_level_fill() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Sell, 100, 5), fills);
    book.add_order(limit(2, Side::Sell, 101, 5), fills);
    book.add_order(limit(3, Side::Sell, 102, 5), fills);
    fills.clear();

    book.add_order(limit(4, Side::Buy, 102, 12), fills);
    assert(fills.size() == 3);
    assert(fills[0].price == 100);
    assert(fills[1].price == 101);
    assert(fills[2].price == 102);
    assert(fills[2].qty == 2);
    assert(book.depth_at(Side::Sell, 102) == 3);
    fprintf(stderr, "  PASS: multi_level_fill\n");
}

static void test_market_order() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Sell, 100, 10), fills);
    book.add_order(limit(2, Side::Sell, 105, 10), fills);
    fills.clear();

    book.add_order(market(3, Side::Buy, 15), fills);
    assert(fills.size() == 2);
    assert(fills[0].price == 100);
    assert(fills[0].qty == 10);
    assert(fills[1].price == 105);
    assert(fills[1].qty == 5);
    assert(book.len() == 1);
    fprintf(stderr, "  PASS: market_order\n");
}

static void test_cancel() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Buy, 100, 10), fills);
    assert(book.len() == 1);

    assert(book.cancel(1));
    assert(book.len() == 0);
    assert(!book.best_bid().has_value());

    assert(!book.cancel(1));
    fprintf(stderr, "  PASS: cancel\n");
}

static void test_cancel_nonexistent() {
    OrderBook book;
    assert(!book.cancel(999));
    fprintf(stderr, "  PASS: cancel_nonexistent\n");
}

static void test_sell_market() {
    OrderBook book;
    std::vector<Fill> fills;

    book.add_order(limit(1, Side::Buy, 100, 10), fills);
    book.add_order(limit(2, Side::Buy, 99, 10), fills);
    fills.clear();

    book.add_order(market(3, Side::Sell, 15), fills);
    assert(fills.size() == 2);
    assert(fills[0].price == 100);
    assert(fills[0].qty == 10);
    assert(fills[1].price == 99);
    assert(fills[1].qty == 5);
    fprintf(stderr, "  PASS: sell_market\n");
}

int main() {
    fprintf(stderr, "\n[matching_engine tests]\n");
    test_no_match_wide_spread();
    test_exact_fill();
    test_partial_fill();
    test_price_improvement();
    test_fifo_priority();
    test_multi_level_fill();
    test_market_order();
    test_cancel();
    test_cancel_nonexistent();
    test_sell_market();
    fprintf(stderr, "\nAll tests passed.\n\n");
    return 0;
}
