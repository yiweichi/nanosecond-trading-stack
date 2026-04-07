#include "nts/market_data.h"
#include "nts/orderbook.h"
#include "nts/strategy.h"
#include "nts/oms.h"
#include "nts/exchange.h"
#include "nts/instrument/tracer.h"
#include "nts/instrument/stats.h"
#include "nts/instrument/clock.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cmath>

static volatile sig_atomic_t running = 1;

static void on_signal(int) { running = 0; }

int main(int argc, char* argv[]) {
    uint16_t port         = nts::DEFAULT_PORT;
    int      duration_sec = 10;

    if (argc > 1) port         = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2) duration_sec = std::atoi(argv[2]);

    signal(SIGINT,  on_signal);
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

    while (running) {
        uint64_t now = nts::instrument::now_ns();
        if (duration_sec > 0 && now >= deadline_ns) break;

        using nts::instrument::Hop;

        nts::MdMsg msg;

        tracer.start_trace();
        tracer.record(Hop::RecvStart);

        bool got_data = md.poll(msg);

        if (got_data) {
            tracer.record(Hop::RecvDone);

            book.update(msg);
            tracer.record(Hop::BookUpdated);

            nts::Signal sig = strategy.on_book_update(book);
            tracer.record(Hop::StrategyDone);

            if (sig != nts::Signal::None) {
                nts::Side side = (sig == nts::Signal::Buy)
                                     ? nts::Side::Buy : nts::Side::Sell;
                double price = (side == nts::Side::Buy)
                                   ? book.ask_price : book.bid_price;

                const nts::Order* order =
                    oms.create_order(side, price, nts::DEFAULT_ORDER_SIZE);
                if (order) {
                    exchange.submit_order(*order);
                }
                tracer.record(Hop::OrderSent);
            }

            nts::Ack ack;
            if (exchange.poll_ack(ack)) {
                tracer.record(Hop::AckReceived);
                oms.on_ack(ack);
                tracer.record(Hop::AckProcessed);
            }

            tracer.end_trace();
        } else {
            tracer.discard_trace();

            nts::Ack ack;
            if (exchange.poll_ack(ack)) {
                oms.on_ack(ack);
            }
        }

        iterations++;
    }

    md.close();

    double elapsed_s = static_cast<double>(nts::instrument::now_ns() - start_ns)
                     / 1'000'000'000.0;

    fprintf(stderr, "\n[pipeline] stopped after %.2f seconds, %llu iterations\n",
            elapsed_s, static_cast<unsigned long long>(iterations));
    fprintf(stderr, "[pipeline] packets received: %llu, gaps detected: %llu\n",
            static_cast<unsigned long long>(md.packets_received()),
            static_cast<unsigned long long>(md.packets_dropped()));
    fprintf(stderr, "[pipeline] orders: %zu total, %zu filled, position: %d\n",
            oms.total_orders(), oms.filled_count(), oms.position());

    nts::instrument::StatsCalculator::print_report(tracer);

    return 0;
}
