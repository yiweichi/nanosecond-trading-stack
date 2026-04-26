#include "nts/exchange.h"
#include "nts/instrument/clock.h"
#include "nts/instrument/stats.h"
#include "nts/instrument/tracer.h"
#include "nts/market_data.h"
#include "nts/oms.h"
#include "nts/orderbook.h"
#include "nts/strategy.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <string>

static nts::MdQuote make_quote(double& price, uint32_t& seq, std::mt19937& rng,
                               std::normal_distribution<double>&        price_step,
                               std::uniform_int_distribution<uint32_t>& size_dist) {
    price += price_step(rng);

    nts::MdQuote q;
    std::memset(&q, 0, sizeof(q));
    q.header.timestamp_ns  = nts::instrument::now_ns();
    q.header.instrument_id = nts::DEFAULT_INSTRUMENT;
    q.header.sequence_num  = seq++;
    q.header.type          = nts::MdMsgType::Quote;
    q.bid_price            = price - 0.01;
    q.ask_price            = price + 0.01;
    q.bid_size             = size_dist(rng);
    q.ask_size             = size_dist(rng);
    return q;
}

static nts::MdReference make_reference(double price, uint32_t& seq) {
    nts::MdReference r;
    std::memset(&r, 0, sizeof(r));
    r.header.timestamp_ns  = nts::instrument::now_ns();
    r.header.instrument_id = nts::DEFAULT_INSTRUMENT;
    r.header.sequence_num  = seq++;
    r.header.type          = nts::MdMsgType::Reference;
    r.reference_mid        = price;
    return r;
}

struct PipelineState {
    nts::OrderBook         book;
    nts::ImbalanceStrategy strategy;
    nts::OMS               oms;
    nts::MockExchange      exchange;

    explicit PipelineState(const nts::StrategyParams& params) : strategy(params) {}
};

static void run_step(PipelineState& st, const nts::MdReference& r, const nts::MdQuote& q) {
    st.book.on_reference(r);
    st.book.on_quote(q);

    if (st.book.valid()) st.oms.set_reference_price(st.book.mid_price());

    nts::Signal sig = st.strategy.on_book_update(st.book, st.oms.net_position());

    if (sig != nts::Signal::None && st.book.valid()) {
        nts::Side  side  = (sig == nts::Signal::Buy) ? nts::Side::Buy : nts::Side::Sell;
        nts::Price price = (side == nts::Side::Buy) ? st.book.best_ask() : st.book.best_bid();

        nts::Order* order = st.oms.send_new(side, price, st.strategy.order_size(), nts::OrderType::IOC);
        if (order != nullptr) st.exchange.submit_order(*order);
    }

    nts::ExecutionReport exec;
    while (st.exchange.poll_execution(exec)) {
        st.oms.on_execution(exec);
    }
}

static void run_step_traced(PipelineState& st, const nts::MdReference& r, const nts::MdQuote& q,
                            nts::instrument::HopTracer& tracer) {
    using nts::instrument::Hop;

    tracer.start_trace();
    tracer.record(Hop::RecvDone);

    st.book.on_reference(r);
    st.book.on_quote(q);
    tracer.record(Hop::BookUpdated);

    if (st.book.valid()) st.oms.set_reference_price(st.book.mid_price());

    nts::Signal sig = st.strategy.on_book_update(st.book, st.oms.net_position());
    tracer.record(Hop::StrategyDone);

    if (sig != nts::Signal::None && st.book.valid()) {
        nts::Side  side  = (sig == nts::Signal::Buy) ? nts::Side::Buy : nts::Side::Sell;
        nts::Price price = (side == nts::Side::Buy) ? st.book.best_ask() : st.book.best_bid();

        nts::Order* order = st.oms.send_new(side, price, st.strategy.order_size(), nts::OrderType::IOC);
        if (order != nullptr) st.exchange.submit_order(*order);
        tracer.record(Hop::OrderSent);
    }

    nts::ExecutionReport exec;
    while (st.exchange.poll_execution(exec)) {
        tracer.record(Hop::AckReceived);
        st.oms.on_execution(exec);
        tracer.record(Hop::AckProcessed);
    }

    tracer.end_trace();
}

int main(int argc, char* argv[]) {
    size_t iterations = 100'000;
    size_t warmup     = 10'000;

    if (argc > 1) iterations = static_cast<size_t>(std::stoul(argv[1]));
    if (argc > 2) warmup = static_cast<size_t>(std::stoul(argv[2]));

    fprintf(stderr, "[bench] iterations=%zu, warmup=%zu\n", iterations, warmup);

    PipelineState              state(nts::StrategyParams{});
    nts::instrument::HopTracer tracer(iterations);

    std::mt19937                            rng(42);
    std::normal_distribution<double>        price_step(0.0, 0.01);
    std::uniform_int_distribution<uint32_t> size_dist(100, 1000);
    double                                  price = 100.0;
    uint32_t                                seq   = 0;

    for (size_t i = 0; i < warmup; i++) {
        nts::MdReference r = make_reference(price, seq);
        nts::MdQuote     q = make_quote(price, seq, rng, price_step, size_dist);
        run_step(state, r, q);
    }

    state = PipelineState(nts::StrategyParams{});

    uint64_t bench_start = nts::instrument::now_ns();

    for (size_t i = 0; i < iterations; i++) {
        nts::MdReference r = make_reference(price, seq);
        nts::MdQuote     q = make_quote(price, seq, rng, price_step, size_dist);
        run_step_traced(state, r, q, tracer);
    }

    uint64_t bench_end  = nts::instrument::now_ns();
    double   elapsed_ms = static_cast<double>(bench_end - bench_start) / 1'000'000.0;

    fprintf(stderr, "\n[bench] completed in %.2f ms (%.0f ns/iteration avg)\n", elapsed_ms,
            (elapsed_ms * 1'000'000.0) / static_cast<double>(iterations));
    fprintf(stderr, "[bench] orders: %zu total, %zu filled, %zu cancelled, %zu rejected\n",
            state.oms.total_orders(), state.oms.total_fills(), state.oms.total_cancels(),
            state.oms.total_rejects());
    fprintf(stderr, "[bench] position: %d, realized PnL: %.4f, total PnL: %.4f\n",
            state.oms.net_position(), state.oms.realized_pnl(),
            state.oms.total_pnl(state.book.mid_price()));

    nts::instrument::StatsCalculator::print_report(tracer);

    // Save results to file
    {
#if defined(__APPLE__)
        const char* dir = "results/pipeline/mac";
#elif defined(__linux__)
        const char* dir = "results/pipeline/linux";
#else
        const char* dir = "results/pipeline/other";
#endif
        std::string dir_str = dir;
        std::string partial;
        for (char c : dir_str) {
            partial += c;
            if (c == '/') mkdir(partial.c_str(), 0755);
        }
        mkdir(dir, 0755);

        time_t    now = time(nullptr);
        struct tm t;
        gmtime_r(&now, &t);
        char ts[32];
        snprintf(ts, sizeof(ts), "%04d%02d%02dT%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1,
                 t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

        std::string path = std::string(dir) + "/" + ts + ".txt";
        FILE*       f    = fopen(path.c_str(), "w");
        if (f != nullptr) {
            fprintf(f, "=== Pipeline Latency Benchmark (C++) ===\n");
            char hash[64] = "";
            // NOLINTNEXTLINE(bugprone-command-processor)
            FILE* p = popen("git rev-parse --short HEAD 2>/dev/null", "r");
            if (p != nullptr) {
                if (fgets(hash, sizeof(hash), p) != nullptr) {
                    size_t len = strlen(hash);
                    if (len > 0 && hash[len - 1] == '\n') hash[len - 1] = '\0';
                }
                pclose(p);
            }
            fprintf(f, "    git: %s\n", hash);
            fprintf(f, "    iterations=%zu  warmup=%zu\n\n", iterations, warmup);
            fprintf(f, "completed in %.2f ms (%.0f ns/iteration avg)\n", elapsed_ms,
                    (elapsed_ms * 1'000'000.0) / static_cast<double>(iterations));
            fprintf(f, "orders: %zu total, %zu filled, %zu cancelled, %zu rejected\n",
                    state.oms.total_orders(), state.oms.total_fills(), state.oms.total_cancels(),
                    state.oms.total_rejects());
            fprintf(f, "position: %d, realized PnL: %.4f, total PnL: %.4f\n\n",
                    state.oms.net_position(), state.oms.realized_pnl(),
                    state.oms.total_pnl(state.book.mid_price()));
            nts::instrument::StatsCalculator::print_report(tracer, f);
            fclose(f);
            fprintf(stderr, "  Results saved to %s\n", path.c_str());
        }
    }

    return 0;
}
