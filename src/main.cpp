#include "nts/instrument/clock.h"
#include "nts/instrument/stats.h"
#include "nts/instrument/tracer.h"
#include "nts/market_data.h"
#include "nts/oms.h"
#include "nts/order_gateway.h"
#include "nts/orderbook.h"
#include "nts/strategy.h"

#include <sys/stat.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>

static volatile sig_atomic_t running = 1;

static void on_signal(int) {
    running = 0;
}

[[maybe_unused]] static void spin_for_ns(uint64_t delay_ns) {
    const uint64_t start = nts::instrument::now_ns();
    while (nts::instrument::now_ns() - start < delay_ns) {}
}

static constexpr nts::Price EXIT_HALF_SPREAD = 1.0;

static bool build_exit_order(const nts::OrderBook& book, int32_t position, nts::Side& side,
                             nts::Price& price, nts::Qty& qty) {
    if (position == 0 || !book.has_reference()) return false;

    const nts::Price reference = book.reference_mid();
    if (position > 0) {
        side  = nts::Side::Sell;
        price = book.best_bid();
        qty   = static_cast<nts::Qty>(position);
        return price >= reference - EXIT_HALF_SPREAD;
    }

    side  = nts::Side::Buy;
    price = book.best_ask();
    qty   = static_cast<nts::Qty>(-position);
    return price <= reference + EXIT_HALF_SPREAD;
}

struct Args {
    uint16_t    md_port       = nts::DEFAULT_PORT;
    const char* md_group      = nts::MdReceiver::DEFAULT_MULTICAST_GROUP;
    uint16_t    ref_port      = 12347;
    const char* ref_group     = "239.1.1.2";
    int         duration_sec  = 10;
    const char* exchange_host = "127.0.0.1";
    uint16_t    order_port    = nts::OrderGateway::DEFAULT_ORDER_PORT;
    bool        save_report   = false;
};

static Args parse_args(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--exchange-host") == 0 && i + 1 < argc) {
            a.exchange_host = argv[++i];
        } else if (std::strcmp(argv[i], "--order-port") == 0 && i + 1 < argc) {
            a.order_port = static_cast<uint16_t>(std::strtol(argv[++i], nullptr, 10));
        } else if ((std::strcmp(argv[i], "--port") == 0 ||
                    std::strcmp(argv[i], "--md-port") == 0) &&
                   i + 1 < argc) {
            a.md_port = static_cast<uint16_t>(std::strtol(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--md-group") == 0 && i + 1 < argc) {
            a.md_group = argv[++i];
        } else if (std::strcmp(argv[i], "--ref-port") == 0 && i + 1 < argc) {
            a.ref_port = static_cast<uint16_t>(std::strtol(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--ref-group") == 0 && i + 1 < argc) {
            a.ref_group = argv[++i];
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            a.duration_sec = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--save-report") == 0) {
            a.save_report = true;
        }
    }
    return a;
}

static const char* live_results_dir() {
#if defined(__APPLE__)
    return "results/live/mac";
#elif defined(__linux__)
    return "results/live/linux";
#else
    return "results/live/other";
#endif
}

static void mkdirs(const std::string& path) {
    std::string partial;
    for (char c : path) {
        partial += c;
        if (c == '/') mkdir(partial.c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
}

static std::string utc_timestamp() {
    time_t    now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char ts[32];
    snprintf(ts, sizeof(ts), "%04d%02d%02dT%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return ts;
}

static void print_trading_report(const nts::MdReceiver& ref_md, const nts::MdReceiver& target_md,
                                 nts::OMS& oms, const nts::OrderBook& book, double elapsed_s,
                                 uint64_t iterations, FILE* out = stderr) {
    fprintf(out, "\n[pipeline] stopped after %.2f seconds, %llu iterations\n", elapsed_s,
            static_cast<unsigned long long>(iterations));
    fprintf(out, "[pipeline] reference packets: %llu recv, %llu gaps | refs: %llu\n",
            static_cast<unsigned long long>(ref_md.packets_received()),
            static_cast<unsigned long long>(ref_md.packets_dropped()),
            static_cast<unsigned long long>(ref_md.references_received()));
    fprintf(out, "[pipeline] target MD packets: %llu recv, %llu gaps | quotes: %llu\n",
            static_cast<unsigned long long>(target_md.packets_received()),
            static_cast<unsigned long long>(target_md.packets_dropped()),
            static_cast<unsigned long long>(target_md.quotes_received()));
    double hit_rate = (oms.total_accepted_orders() > 0)
                          ? 100.0 * static_cast<double>(oms.total_filled_orders()) /
                                static_cast<double>(oms.total_accepted_orders())
                          : 0.0;
    fprintf(out,
            "[pipeline] Orders: %zu total, %zu accepted, %zu filled, %zu missed IOC, %zu rejected "
            "(%.1f%% hit)\n",
            oms.total_orders(), oms.total_accepted_orders(), oms.total_filled_orders(),
            oms.total_missed_ioc(), oms.total_rejects(), hit_rate);
    fprintf(
        out,
        "[pipeline] Fills:  %zu events, %u qty (%zu buys / %u buy qty, %zu sells / %u sell qty)\n",
        oms.total_fills(), oms.total_filled_qty(), oms.total_buy_fills(), oms.total_buy_qty(),
        oms.total_sell_fills(), oms.total_sell_qty());
    fprintf(out, "[pipeline] PnL:    realized %.4f, liquidation %.4f, total %.4f\n",
            oms.total_pnl(book.mid_price()) - oms.liquidation_pnl(), oms.liquidation_pnl(),
            oms.total_pnl(book.mid_price()));
}

/// Isolated market-data -> strategy -> order path for PMU attribution.
extern "C" __attribute__((no_stack_protector)) NTS_PROFILE_NOINLINE void process_market_signal_and_order(
    bool got_ref_data, const nts::MdMsg& ref_msg, uint64_t ref_receive_ticks, bool got_target_data,
    const nts::MdMsg& target_msg, uint64_t target_receive_ticks, nts::OrderBook& book,
    nts::ImbalanceStrategy& strategy, nts::OMS& oms, nts::OrderGateway& exchange,
    nts::instrument::ActiveTracer& tracer, uint64_t& latest_source_exchange_tick,
    uint64_t& latest_md_receive_ticks,
    std::unordered_map<nts::OrderId, uint64_t>& order_sent_ticks) {
    using nts::instrument::Hop;

    bool reference_updated = false;
    bool last_md_was_quote = false;

    if (got_ref_data) {
        if (ref_msg.header.type == nts::MdMsgType::Reference) {
            latest_source_exchange_tick = ref_msg.header.exchange_tick;
            latest_md_receive_ticks     = ref_receive_ticks;
            book.on_reference(ref_msg.reference);
            reference_updated = true;
        }
    }

    if (got_target_data) {
        switch (target_msg.header.type) {
            case nts::MdMsgType::Quote:
                latest_source_exchange_tick = target_msg.header.exchange_tick;
                latest_md_receive_ticks     = target_receive_ticks;
                book.on_quote(target_msg.quote);
                last_md_was_quote = true;
                break;
            case nts::MdMsgType::Reference: break;
        }
    }
    tracer.record(Hop::BookUpdated);

    oms.set_reference_price(book.mid_price());

    nts::Signal sig               = nts::Signal::None;
    int32_t     position          = oms.net_position();
    bool        has_pending_order = oms.pending_count() > 0;

    nts::Side  exit_side  = nts::Side::Buy;
    nts::Price exit_price = 0.0;
    nts::Qty   exit_qty   = 0;

    bool should_exit =
        !has_pending_order && build_exit_order(book, position, exit_side, exit_price, exit_qty);

    if (position == 0 && !should_exit && !has_pending_order &&
        (reference_updated || last_md_was_quote)) {
        sig = strategy.on_book_update(book, position);
    }
    tracer.record(Hop::StrategyDone);

    auto record_ready_order = [&](nts::Order& order) {
        const uint64_t sent_ticks  = nts::instrument::raw_ticks();
        order.source_exchange_tick = latest_source_exchange_tick;
        order.client_reaction_ns =
            (latest_md_receive_ticks != 0 && sent_ticks >= latest_md_receive_ticks)
                ? nts::instrument::ticks_to_ns(sent_ticks - latest_md_receive_ticks)
                : 0;
        tracer.record_at(nts::instrument::Hop::OrderSent, sent_ticks);
        order_sent_ticks[order.id] = sent_ticks;
    };

    if (should_exit) {
        nts::Order* order = oms.send_new(exit_side, exit_price, exit_qty, nts::OrderType::IOC);
        if (order != nullptr) {
            record_ready_order(*order);
            exchange.submit_order(*order);
        }
    } else if (sig != nts::Signal::None) {
        nts::Side  side  = (sig == nts::Signal::Buy) ? nts::Side::Buy : nts::Side::Sell;
        nts::Price price = (side == nts::Side::Buy) ? book.best_ask() : book.best_bid();

        nts::Order* order = oms.send_new(side, price, strategy.order_size(), nts::OrderType::IOC);
        if (order != nullptr) {
            record_ready_order(*order);
            exchange.submit_order(*order);
        }
    }
}

/// Core pipeline loop.
extern "C" NTS_NOINLINE void run_pipeline(nts::MdReceiver& ref_md, nts::MdReceiver& target_md,
                                          nts::OrderBook& book,
                                          nts::ImbalanceStrategy& strategy, nts::OMS& oms,
                                          nts::OrderGateway& exchange,
                                          nts::instrument::ActiveTracer& tracer, int duration_sec,
                                          bool save_report) {
    uint64_t start_ns    = nts::instrument::now_ns();
    uint64_t deadline_ns = start_ns + static_cast<uint64_t>(duration_sec) * 1'000'000'000ULL;
    uint64_t iterations  = 0;

    std::unordered_map<nts::OrderId, uint64_t> order_sent_ticks;
    std::unordered_map<nts::OrderId, uint64_t> ack_received_ticks;
    order_sent_ticks.reserve(nts::OMS::MAX_ORDERS);
    ack_received_ticks.reserve(nts::OMS::MAX_ORDERS);

    uint64_t latest_source_exchange_tick = 0;
    uint64_t latest_md_receive_ticks     = 0;

    auto process_execution = [&](const nts::ExecutionReport& exec) {
        const uint64_t report_received_ticks = nts::instrument::raw_ticks();
        auto           sent_it               = order_sent_ticks.find(exec.order_id);
        auto           ack_it                = ack_received_ticks.find(exec.order_id);

        oms.on_execution(exec);

        const uint64_t report_processed_ticks = nts::instrument::raw_ticks();
        switch (exec.exec_type) {
            case nts::ExecType::NewAck:
                if (sent_it != order_sent_ticks.end()) {
                    tracer.record_order_ack(sent_it->second, report_received_ticks,
                                            report_processed_ticks);
                    order_sent_ticks.erase(sent_it);
                    ack_received_ticks[exec.order_id] = report_received_ticks;
                }
                break;
            case nts::ExecType::Fill:
            case nts::ExecType::PartialFill:
            case nts::ExecType::Reject:
            case nts::ExecType::CancelAck:
            case nts::ExecType::CancelReject:
                if (ack_it != ack_received_ticks.end()) {
                    tracer.record_ack_fill(ack_it->second, report_received_ticks,
                                           report_processed_ticks);
                    ack_received_ticks.erase(ack_it);
                }
                order_sent_ticks.erase(exec.order_id);
                break;
        }
    };

    while (running != 0) {
        uint64_t now = nts::instrument::now_ns();
        if (duration_sec > 0 && now >= deadline_ns) break;

        using nts::instrument::Hop;

        nts::MdMsg ref_msg;
        nts::MdMsg target_msg;

        tracer.start_trace();
        tracer.record(Hop::RecvStart);

        bool     got_ref_data         = ref_md.poll(ref_msg);
        uint64_t ref_receive_ticks    = got_ref_data ? nts::instrument::raw_ticks() : 0;
        bool     got_target_data      = target_md.poll(target_msg);
        uint64_t target_receive_ticks = got_target_data ? nts::instrument::raw_ticks() : 0;
        bool     got_data             = got_ref_data || got_target_data;

        if (got_data) {
            tracer.record(Hop::RecvDone);

            // spin_for_ns(50000);

            process_market_signal_and_order(got_ref_data, ref_msg, ref_receive_ticks,
                                            got_target_data, target_msg, target_receive_ticks,
                                            book, strategy, oms, exchange, tracer,
                                            latest_source_exchange_tick, latest_md_receive_ticks,
                                            order_sent_ticks);

            nts::ExecutionReport exec;
            while (exchange.poll_execution(exec)) {
                process_execution(exec);
            }

            tracer.end_trace();
        } else {
            tracer.discard_trace();

            nts::ExecutionReport exec;
            while (exchange.poll_execution(exec)) {
                process_execution(exec);
            }
        }

        iterations++;
    }

    double elapsed_s = static_cast<double>(nts::instrument::now_ns() - start_ns) / 1'000'000'000.0;

#ifdef NTS_ENABLE_TRACING
    nts::instrument::StatsCalculator::print_report(tracer);
#endif
    print_trading_report(ref_md, target_md, oms, book, elapsed_s, iterations);

    if (save_report) {
        const std::string dir = live_results_dir();
        mkdirs(dir);
        const std::string path = dir + "/" + utc_timestamp() + ".txt";
        FILE*             f    = fopen(path.c_str(), "w");
        if (f == nullptr) {
            perror("fopen save report");
            return;
        }
#ifdef NTS_ENABLE_TRACING
        nts::instrument::StatsCalculator::print_report(tracer, f);
#endif
        print_trading_report(ref_md, target_md, oms, book, elapsed_s, iterations, f);
        fclose(f);
        fprintf(stderr, "[pipeline] report saved to %s\n", path.c_str());
    }
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    nts::MdReceiver        ref_md;
    nts::MdReceiver        target_md;
    nts::OrderBook         book;
    nts::ImbalanceStrategy strategy(nts::StrategyParams{});
    nts::OMS               oms;

    nts::instrument::ActiveTracer tracer;

    if (!ref_md.init(args.ref_port, args.ref_group)) return 1;
    if (!target_md.init(args.md_port, args.md_group)) return 1;

    nts::OrderGateway gateway;
    if (!gateway.connect(args.exchange_host, args.order_port)) return 1;

    fprintf(stderr, "[pipeline] LIVE mode — exchange=%s:%u, ref=%s:%u, md=%s:%u, duration=%ds\n",
            args.exchange_host, args.order_port, args.ref_group, args.ref_port, args.md_group,
            args.md_port, args.duration_sec);

    run_pipeline(ref_md, target_md, book, strategy, oms, gateway, tracer, args.duration_sec,
                 args.save_report);

    gateway.close();

    target_md.close();
    ref_md.close();

    return 0;
}
