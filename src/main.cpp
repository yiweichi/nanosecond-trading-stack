#include "nts/exchange.h"
#include "nts/instrument/clock.h"
#include "nts/instrument/stats.h"
#include "nts/instrument/tracer.h"
#include "nts/market_data.h"
#include "nts/oms.h"
#include "nts/orderbook.h"
#include "nts/strategy.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

static volatile sig_atomic_t running = 1;

static void on_signal(int) {
    running = 0;
}

int main(int argc, char* argv[]) {
    uint16_t port         = nts::DEFAULT_PORT;
    int      duration_sec = 10;

    if (argc > 1) port = static_cast<uint16_t>(std::strtol(argv[1], nullptr, 10));
    if (argc > 2) duration_sec = static_cast<int>(std::strtol(argv[2], nullptr, 10));

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    nts::MdReceiver        md;
    nts::OrderBook         book;
    nts::ImbalanceStrategy strategy(nts::StrategyParams{});
    nts::OMS               oms;
    nts::MockExchange      exchange;

    nts::instrument::HopTracer tracer;

    if (!md.init(port)) return 1;

    fprintf(stderr, "[pipeline] running for %d seconds (Ctrl-C to stop early)\n", duration_sec);

    uint64_t start_ns    = nts::instrument::now_ns();
    uint64_t deadline_ns = start_ns + static_cast<uint64_t>(duration_sec) * 1'000'000'000ULL;
    uint64_t iterations  = 0;

    while (running != 0) {
        uint64_t now = nts::instrument::now_ns();
        if (duration_sec > 0 && now >= deadline_ns) break;

        using nts::instrument::Hop;

        nts::MdMsg msg;

        tracer.start_trace();
        tracer.record(Hop::RecvStart);

        bool got_data = md.poll(msg);

        if (got_data) {
            tracer.record(Hop::RecvDone);

            switch (msg.header.type) {
                case nts::MdMsgType::Quote: book.on_quote(msg.quote); break;
                case nts::MdMsgType::Depth: book.on_depth(msg.depth); break;
                case nts::MdMsgType::Trade: book.on_trade(msg.trade); break;
            }
            tracer.record(Hop::BookUpdated);

            if (book.valid()) oms.set_reference_price(book.mid_price());

            nts::Signal sig = strategy.on_book_update(book);
            tracer.record(Hop::StrategyDone);

            if (sig != nts::Signal::None && book.valid()) {
                nts::Side  side  = (sig == nts::Signal::Buy) ? nts::Side::Buy : nts::Side::Sell;
                nts::Price price = (side == nts::Side::Buy) ? book.best_ask() : book.best_bid();

                nts::Order* order = oms.send_new(side, price, nts::DEFAULT_ORDER_SIZE);
                if (order != nullptr) {
                    exchange.submit_order(*order);
                }
                tracer.record(Hop::OrderSent);
            }

            nts::ExecutionReport exec;
            while (exchange.poll_execution(exec)) {
                tracer.record(Hop::AckReceived);
                oms.on_execution(exec);
                tracer.record(Hop::AckProcessed);
            }

            tracer.end_trace();
        } else {
            tracer.discard_trace();

            nts::ExecutionReport exec;
            if (exchange.poll_execution(exec)) {
                oms.on_execution(exec);
            }
        }

        iterations++;
    }

    md.close();

    double elapsed_s = static_cast<double>(nts::instrument::now_ns() - start_ns) / 1'000'000'000.0;

    fprintf(stderr, "\n[pipeline] stopped after %.2f seconds, %llu iterations\n", elapsed_s,
            static_cast<unsigned long long>(iterations));
    fprintf(stderr,
            "[pipeline] packets: %llu recv, %llu gaps | quotes: %llu, depths: %llu, trades: %llu\n",
            static_cast<unsigned long long>(md.packets_received()),
            static_cast<unsigned long long>(md.packets_dropped()),
            static_cast<unsigned long long>(md.quotes_received()),
            static_cast<unsigned long long>(md.depths_received()),
            static_cast<unsigned long long>(md.trades_received()));
    fprintf(stderr, "[pipeline] orders: %zu total, %zu filled, %zu cancelled, %zu rejected\n",
            oms.total_orders(), oms.total_fills(), oms.total_cancels(), oms.total_rejects());
    fprintf(stderr, "[pipeline] position: %d, realized PnL: %.4f, total PnL: %.4f\n",
            oms.net_position(), oms.realized_pnl(), oms.total_pnl(book.mid_price()));

    nts::instrument::StatsCalculator::print_report(tracer);

    return 0;
}
