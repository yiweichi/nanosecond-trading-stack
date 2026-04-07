#include "nts/market_data.h"
#include "nts/orderbook.h"
#include "nts/strategy.h"
#include "nts/oms.h"
#include "nts/exchange.h"
#include "nts/instrument/tracer.h"
#include "nts/instrument/stats.h"
#include "nts/instrument/clock.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <random>

static nts::MdMsg make_md(double& price, uint32_t& seq,
                          std::mt19937& rng,
                          std::normal_distribution<double>& price_step,
                          std::uniform_int_distribution<uint32_t>& size_dist) {
    price += price_step(rng);

    nts::MdMsg msg;
    msg.timestamp_ns  = nts::instrument::now_ns();
    msg.instrument_id = nts::DEFAULT_INSTRUMENT;
    msg.sequence_num  = seq++;
    msg.bid_price     = price - 0.01;
    msg.ask_price     = price + 0.01;
    msg.bid_size      = size_dist(rng);
    msg.ask_size      = size_dist(rng);
    return msg;
}

struct PipelineState {
    nts::OrderBook         book;
    nts::ImbalanceStrategy strategy;
    nts::OMS               oms;
    nts::MockExchange      exchange;

    explicit PipelineState(const nts::StrategyParams& params)
        : strategy(params) {}
};

static void run_pipeline_step(PipelineState& state, const nts::MdMsg& msg) {
    state.book.update(msg);

    nts::Signal sig = state.strategy.on_book_update(state.book);

    if (sig != nts::Signal::None) {
        nts::Side side = (sig == nts::Signal::Buy)
                             ? nts::Side::Buy : nts::Side::Sell;
        double price = (side == nts::Side::Buy)
                           ? state.book.ask_price : state.book.bid_price;

        if (std::abs(state.oms.position()) < nts::MAX_POSITION) {
            const nts::Order* order =
                state.oms.create_order(side, price, nts::DEFAULT_ORDER_SIZE);
            if (order) {
                state.exchange.submit_order(*order);
            }
        }
    }

    nts::Ack ack;
    if (state.exchange.poll_ack(ack)) {
        state.oms.on_ack(ack);
    }
}

// Traced version: identical logic, but with tracer calls inserted.
// Kept as a separate function so the untraced warmup path has zero
// measurement overhead — no pointer checks, no dead branches.
static void run_pipeline_step_traced(PipelineState& state,
                                      const nts::MdMsg& msg,
                                      nts::instrument::HopTracer& tracer) {
    using nts::instrument::Hop;

    tracer.start_trace();
    tracer.record(Hop::RecvDone);

    state.book.update(msg);
    tracer.record(Hop::BookUpdated);

    nts::Signal sig = state.strategy.on_book_update(state.book);
    tracer.record(Hop::StrategyDone);

    if (sig != nts::Signal::None) {
        nts::Side side = (sig == nts::Signal::Buy)
                             ? nts::Side::Buy : nts::Side::Sell;
        double price = (side == nts::Side::Buy)
                           ? state.book.ask_price : state.book.bid_price;

        if (std::abs(state.oms.position()) < nts::MAX_POSITION) {
            const nts::Order* order =
                state.oms.create_order(side, price, nts::DEFAULT_ORDER_SIZE);
            if (order) {
                state.exchange.submit_order(*order);
            }
        }
        tracer.record(Hop::OrderSent);
    }

    nts::Ack ack;
    if (state.exchange.poll_ack(ack)) {
        tracer.record(Hop::AckReceived);
        state.oms.on_ack(ack);
        tracer.record(Hop::AckProcessed);
    }

    tracer.end_trace();
}

int main(int argc, char* argv[]) {
    size_t iterations = 100'000;
    size_t warmup     = 10'000;

    if (argc > 1) iterations = static_cast<size_t>(std::stoul(argv[1]));
    if (argc > 2) warmup     = static_cast<size_t>(std::stoul(argv[2]));

    fprintf(stderr, "[bench] iterations=%zu, warmup=%zu\n", iterations, warmup);

    PipelineState state(nts::StrategyParams{});
    nts::instrument::HopTracer tracer(iterations);

    std::mt19937 rng(42);
    std::normal_distribution<double>        price_step(0.0, 0.01);
    std::uniform_int_distribution<uint32_t> size_dist(100, 1000);
    double   price = 100.0;
    uint32_t seq   = 0;

    // ── Warmup (no tracing, no pointer checks — clean code path) ────────
    for (size_t i = 0; i < warmup; i++) {
        nts::MdMsg msg = make_md(price, seq, rng, price_step, size_dist);
        run_pipeline_step(state, msg);
    }

    // Reset state after warmup
    state = PipelineState(nts::StrategyParams{});

    // ── Benchmark (with tracing) ────────────────────────────────────────
    uint64_t bench_start = nts::instrument::now_ns();

    for (size_t i = 0; i < iterations; i++) {
        nts::MdMsg msg = make_md(price, seq, rng, price_step, size_dist);
        run_pipeline_step_traced(state, msg, tracer);
    }

    uint64_t bench_end = nts::instrument::now_ns();
    double elapsed_ms = static_cast<double>(bench_end - bench_start) / 1'000'000.0;

    fprintf(stderr, "[bench] completed in %.2f ms (%.0f ns/iteration avg)\n",
            elapsed_ms, (elapsed_ms * 1'000'000.0) / static_cast<double>(iterations));
    fprintf(stderr, "[bench] orders: %zu total, %zu filled, position: %d\n",
            state.oms.total_orders(), state.oms.filled_count(), state.oms.position());

    nts::instrument::StatsCalculator::print_report(tracer);

    return 0;
}
