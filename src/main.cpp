#include "nts/instrument/clock.h"
#include "nts/instrument/stats.h"
#include "nts/instrument/tracer.h"
#include "nts/market_data.h"
#include "nts/oms.h"
#include "nts/order_gateway.h"
#include "nts/orderbook.h"
#include "nts/perf_counter.h"
#include "nts/pipeline_utils.h"
#include "nts/strategy.h"

#include <csignal>

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

#ifdef NTS_ENABLE_PMU_PROFILE
struct PmuProfileTotals {
    uint64_t calls  = 0;
    uint64_t cycles = 0;
};
#endif

/// Isolated market-data -> strategy -> order path for PMU attribution.
extern "C" NTS_PROFILE_NOINLINE void process_market_signal_and_order(
    bool got_ref_data, const nts::MdMsg& ref_msg, uint64_t ref_receive_ticks, bool got_target_data,
    const nts::MdMsg& target_msg, uint64_t target_receive_ticks, nts::OrderBook& book,
    nts::ImbalanceStrategy& strategy, nts::OMS& oms, nts::OrderGateway& exchange,
    nts::instrument::ActiveTracer& tracer, uint64_t& latest_source_exchange_tick,
    uint64_t& latest_md_receive_ticks, nts::OrderTickRing& order_sent_ticks
#ifdef NTS_ENABLE_PMU_PROFILE
    ,
    PmuProfileTotals& pmu_totals
#endif
) {
#ifdef NTS_ENABLE_PMU_PROFILE
    static nts::instrument::PerfCounter cycles(nts::instrument::PerfEvent::CpuCycles);
    cycles.reset();
    cycles.enable();
#endif
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
        order_sent_ticks.set(order.id, sent_ticks);
        tracer.record(nts::instrument::Hop::OrderSent);
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

#ifdef NTS_ENABLE_PMU_PROFILE
    cycles.disable();
    pmu_totals.calls++;
    pmu_totals.cycles += cycles.read_value();
#endif
}

/// Core pipeline loop.
extern "C" NTS_NOINLINE void run_pipeline(nts::MdReceiver& ref_md, nts::MdReceiver& target_md,
                                          nts::OrderBook& book, nts::ImbalanceStrategy& strategy,
                                          nts::OMS& oms, nts::OrderGateway& exchange,
                                          nts::instrument::ActiveTracer& tracer, int duration_sec,
                                          bool save_report) {
    uint64_t start_ns    = nts::instrument::now_ns();
    uint64_t deadline_ns = start_ns + static_cast<uint64_t>(duration_sec) * 1'000'000'000ULL;
    uint64_t iterations  = 0;

    nts::OrderTickRing order_sent_ticks;
    nts::OrderTickRing ack_received_ticks;

    uint64_t latest_source_exchange_tick = 0;
    uint64_t latest_md_receive_ticks     = 0;

#ifdef NTS_ENABLE_PMU_PROFILE
    PmuProfileTotals pmu_totals;
#endif

    auto process_execution = [&](const nts::ExecutionReport& exec) {
        const uint64_t report_received_ticks = nts::instrument::raw_ticks();
        uint64_t       sent_ticks            = 0;
        uint64_t       ack_ticks             = 0;
        bool           has_sent_ticks        = order_sent_ticks.get(exec.order_id, sent_ticks);
        bool           has_ack_ticks         = ack_received_ticks.get(exec.order_id, ack_ticks);

        oms.on_execution(exec);

        const uint64_t report_processed_ticks = nts::instrument::raw_ticks();
        switch (exec.exec_type) {
            case nts::ExecType::NewAck:
                if (has_sent_ticks) {
                    tracer.record_order_ack(sent_ticks, report_received_ticks,
                                            report_processed_ticks);
                    order_sent_ticks.erase(exec.order_id);
                    ack_received_ticks.set(exec.order_id, report_received_ticks);
                }
                break;
            case nts::ExecType::Fill:
            case nts::ExecType::PartialFill:
            case nts::ExecType::Reject:
            case nts::ExecType::CancelAck:
            case nts::ExecType::CancelReject:
                if (has_ack_ticks) {
                    tracer.record_ack_fill(ack_ticks, report_received_ticks,
                                           report_processed_ticks);
                    ack_received_ticks.erase(exec.order_id);
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

            process_market_signal_and_order(
                got_ref_data, ref_msg, ref_receive_ticks, got_target_data, target_msg,
                target_receive_ticks, book, strategy, oms, exchange, tracer,
                latest_source_exchange_tick, latest_md_receive_ticks, order_sent_ticks
#ifdef NTS_ENABLE_PMU_PROFILE
                ,
                pmu_totals
#endif
            );

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
#ifdef NTS_ENABLE_PMU_PROFILE
    fprintf(stderr, "[pmu] process_market_signal_and_order calls=%llu cycles_sum=%llu\n",
            static_cast<unsigned long long>(pmu_totals.calls),
            static_cast<unsigned long long>(pmu_totals.cycles));
#endif
    print_trading_report(ref_md, target_md, oms, book, elapsed_s, iterations);
    print_order_tick_ring_report("order_sent_ticks ring", order_sent_ticks);
    print_order_tick_ring_report("ack_received_ticks ring", ack_received_ticks);

    if (save_report) {
        const std::string dir = nts::live_results_dir();
        nts::mkdirs(dir);
        const std::string path = dir + "/" + nts::utc_timestamp() + ".txt";
        FILE*             f    = fopen(path.c_str(), "w");
        if (f == nullptr) {
            perror("fopen save report");
            return;
        }
#ifdef NTS_ENABLE_TRACING
        nts::instrument::StatsCalculator::print_report(tracer, f);
#endif
#ifdef NTS_ENABLE_PMU_PROFILE
        fprintf(f, "[pmu] process_market_signal_and_order calls=%llu cycles_sum=%llu\n",
                static_cast<unsigned long long>(pmu_totals.calls),
                static_cast<unsigned long long>(pmu_totals.cycles));
#endif
        print_trading_report(ref_md, target_md, oms, book, elapsed_s, iterations, f);
        print_order_tick_ring_report("order_sent_ticks ring", order_sent_ticks, f);
        print_order_tick_ring_report("ack_received_ticks ring", ack_received_ticks, f);
        fclose(f);
        fprintf(stderr, "[pipeline] report saved to %s\n", path.c_str());
    }
}

int main(int argc, char* argv[]) {
    nts::Args args = nts::parse_args(argc, argv);

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
