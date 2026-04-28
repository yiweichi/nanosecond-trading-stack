#include "nts/instrument/clock.h"
#include "nts/instrument/stats.h"
#include "nts/instrument/tracer.h"
#include "nts/market_data.h"
#include "nts/oms.h"
#include "nts/order_gateway.h"
#include "nts/orderbook.h"
#include "nts/strategy.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static volatile sig_atomic_t running = 1;

static void on_signal(int) {
    running = 0;
}

struct Args {
    uint16_t    md_port       = nts::DEFAULT_PORT;
    const char* md_group      = nts::MdReceiver::DEFAULT_MULTICAST_GROUP;
    int         duration_sec  = 10;
    const char* exchange_host = "127.0.0.1";
    uint16_t    order_port    = nts::OrderGateway::DEFAULT_ORDER_PORT;
};

static Args parse_args(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--exchange-host") == 0 && i + 1 < argc) {
            a.exchange_host = argv[++i];
        } else if (std::strcmp(argv[i], "--order-port") == 0 && i + 1 < argc) {
            a.order_port = static_cast<uint16_t>(std::strtol(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            a.md_port = static_cast<uint16_t>(std::strtol(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--md-group") == 0 && i + 1 < argc) {
            a.md_group = argv[++i];
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            a.duration_sec = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
    }
    return a;
}

static void print_trading_report(const nts::MdReceiver& md, nts::OMS& oms, const nts::OrderBook& book,
                                 double elapsed_s, uint64_t iterations) {
    fprintf(stderr, "\n[pipeline] stopped after %.2f seconds, %llu iterations\n", elapsed_s,
            static_cast<unsigned long long>(iterations));
    fprintf(stderr,
            "[pipeline] packets: %llu recv, %llu gaps | refs: %llu, quotes: %llu, depths: %llu, trades: %llu\n",
            static_cast<unsigned long long>(md.packets_received()),
            static_cast<unsigned long long>(md.packets_dropped()),
            static_cast<unsigned long long>(md.references_received()),
            static_cast<unsigned long long>(md.quotes_received()),
            static_cast<unsigned long long>(md.depths_received()),
            static_cast<unsigned long long>(md.trades_received()));
    double hit_rate = (oms.total_accepted_orders() > 0)
                          ? 100.0 * static_cast<double>(oms.total_filled_orders()) /
                                static_cast<double>(oms.total_accepted_orders())
                          : 0.0;
    fprintf(stderr,
            "[pipeline] Orders: %zu total, %zu accepted, %zu filled, %zu missed IOC, %zu rejected (%.1f%% hit)\n",
            oms.total_orders(), oms.total_accepted_orders(), oms.total_filled_orders(),
            oms.total_missed_ioc(), oms.total_rejects(), hit_rate);
    fprintf(stderr,
            "[pipeline] Fills:  %zu events, %u qty (%zu buys / %u buy qty, %zu sells / %u sell qty)\n",
            oms.total_fills(), oms.total_filled_qty(), oms.total_buy_fills(), oms.total_buy_qty(),
            oms.total_sell_fills(), oms.total_sell_qty());
    fprintf(stderr,
            "[pipeline] PnL:    realized %.4f, liquidation %.4f, total %.4f\n",
            oms.total_pnl(book.mid_price()) - oms.liquidation_pnl(), oms.liquidation_pnl(),
            oms.total_pnl(book.mid_price()));
}

/// Core pipeline loop.
template <typename Exchange>
static void run_pipeline(nts::MdReceiver& md, nts::OrderBook& book,
                         nts::ImbalanceStrategy& strategy, nts::OMS& oms,
                         Exchange& exchange, nts::instrument::HopTracer& tracer,
                         int duration_sec) {
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

        bool got_data          = md.poll(msg);
        bool last_md_was_quote = false;

        if (got_data) {
            tracer.record(Hop::RecvDone);

            switch (msg.header.type) {
                case nts::MdMsgType::Quote:
                    book.on_quote(msg.quote);
                    last_md_was_quote = true;
                    break;
                case nts::MdMsgType::Depth: book.on_depth(msg.depth); break;
                case nts::MdMsgType::Trade: book.on_trade(msg.trade); break;
                case nts::MdMsgType::Reference: book.on_reference(msg.reference); break;
            }
            tracer.record(Hop::BookUpdated);

            if (book.valid()) oms.set_reference_price(book.mid_price());

            nts::Signal sig = nts::Signal::None;
            if (last_md_was_quote) {
                sig = strategy.on_book_update(book, oms.net_position());
            }
            tracer.record(Hop::StrategyDone);

            if (sig != nts::Signal::None && book.valid()) {
                nts::Side  side  = (sig == nts::Signal::Buy) ? nts::Side::Buy : nts::Side::Sell;
                nts::Price price = (side == nts::Side::Buy) ? book.best_ask() : book.best_bid();

                nts::Order* order = oms.send_new(side, price, strategy.order_size(), nts::OrderType::IOC);
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
            while (exchange.poll_execution(exec)) {
                oms.on_execution(exec);
            }
        }

        iterations++;
    }

    double elapsed_s = static_cast<double>(nts::instrument::now_ns() - start_ns) / 1'000'000'000.0;

    nts::instrument::StatsCalculator::print_report(tracer);
    print_trading_report(md, oms, book, elapsed_s, iterations);
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    nts::MdReceiver        md;
    nts::OrderBook         book;
    nts::ImbalanceStrategy strategy(nts::StrategyParams{});
    nts::OMS               oms;

    nts::instrument::HopTracer tracer;

    if (!md.init(args.md_port, args.md_group)) return 1;

    nts::OrderGateway gateway;
    if (!gateway.connect(args.exchange_host, args.order_port)) return 1;

    fprintf(stderr, "[pipeline] LIVE mode — exchange=%s:%u, md=%s:%u, duration=%ds\n",
            args.exchange_host, args.order_port, args.md_group, args.md_port, args.duration_sec);

    run_pipeline(md, book, strategy, oms, gateway, tracer, args.duration_sec);

    gateway.close();

    md.close();

    return 0;
}
