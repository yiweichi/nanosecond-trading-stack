#include "nts/instrument/clock.h"
#include "nts/matching/orderbook.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <numeric>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace nts::matching;

// ── Constants (same as Rust) ────────────────────────────────────────────────

static constexpr uint64_t MID         = 10'000;
static constexpr uint64_t SPREAD      = 50;
static constexpr uint64_t WARMUP      = 2'000;
static constexpr uint64_t ITERS       = 200'000;
static constexpr uint64_t SWEEP_ITERS = 50'000;

// ── Histogram ───────────────────────────────────────────────────────────────

struct Histogram {
    std::vector<uint64_t> values;
    bool                  sorted_ = false;

    void reserve(size_t n) { values.reserve(n); }
    void record(uint64_t v) {
        values.push_back(v);
        sorted_ = false;
    }
    size_t len() const { return values.size(); }

    void ensure_sorted() {
        if (!sorted_) {
            std::sort(values.begin(), values.end());
            sorted_ = true;
        }
    }

    uint64_t percentile(double p) {
        ensure_sorted();
        if (values.empty()) return 0;
        auto idx = static_cast<size_t>(p / 100.0 * static_cast<double>(values.size() - 1));
        return values[std::min(idx, values.size() - 1)];
    }

    uint64_t min_val() {
        ensure_sorted();
        return values.empty() ? 0 : values.front();
    }
    uint64_t max_val() {
        ensure_sorted();
        return values.empty() ? 0 : values.back();
    }

    void add(const Histogram& other) {
        values.insert(values.end(), other.values.begin(), other.values.end());
        sorted_ = false;
    }
};

// ── Formatting ──────────────────────────────────────────────────────────────

static std::string fmt_ns(uint64_t n) {
    std::string s = std::to_string(n);
    std::string result;
    result.reserve(s.size() + s.size() / 3);
    int count = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (count > 0 && count % 3 == 0) result.insert(result.begin(), ',');
        result.insert(result.begin(), *it);
        count++;
    }
    return result;
}

static std::string fmt_depth(uint64_t n) {
    if (n >= 1'000'000) return std::to_string(n / 1'000'000) + "M";
    if (n >= 1'000) return std::to_string(n / 1'000) + "K";
    return std::to_string(n);
}

// ── Results directory ────────────────────────────────────────────────────────

static const char* results_dir() {
#if defined(__APPLE__)
    return "results/matching/mac";
#elif defined(__linux__)
    return "results/matching/linux";
#else
    return "results/matching/other";
#endif
}

static std::string fmt_utc_timestamp() {
    time_t     now = time(nullptr);
    struct tm  t;
    gmtime_r(&now, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1,
             t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

static void mkdirs(const std::string& path) {
    std::string partial;
    for (char c : path) {
        partial += c;
        if (c == '/') mkdir(partial.c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
}

// ── Reporter (prints + collects text, saves to file like Rust) ──────────────

struct Reporter {
    std::string                          output;
    Histogram                            combined;
    uint64_t                             total_ops = 0;
    std::string                          current_section;
    std::vector<std::pair<std::string, Histogram>> histograms;

    void git_version() {
        char hash[64]  = "";
        char dirty[16] = "";
        FILE* p = popen("git rev-parse --short HEAD 2>/dev/null", "r");
        if (p != nullptr) {
            if (fgets(hash, sizeof(hash), p) != nullptr) {
                size_t len = strlen(hash);
                if (len > 0 && hash[len - 1] == '\n') hash[len - 1] = '\0';
            }
            pclose(p);
        }
        p = popen("git status --porcelain 2>/dev/null", "r");
        if (p != nullptr) {
            char tmp[8];
            if (fgets(tmp, sizeof(tmp), p) != nullptr) {
                snprintf(dirty, sizeof(dirty), " (dirty)");
            }
            pclose(p);
        }
        char line[128];
        snprintf(line, sizeof(line), "    git: %s%s", hash, dirty);
        header(line);
    }

    void header(const char* text) {
        printf("%s\n", text);
        output += text;
        output += '\n';
    }

    void section(const char* title) {
        current_section = title;
        char buf[512];
        int  prefix_len = 4 + static_cast<int>(strlen(title)) + 6;
        std::string bar(std::max(0, 94 - prefix_len), '\xe2');

        std::string title_bar = "\n\xe2\x94\x80\xe2\x94\x80 ";
        title_bar += title;
        title_bar += " (ns) ";
        for (int i = prefix_len; i < 94; i++) title_bar += "\xe2\x94\x80";
        title_bar += "\n";

        snprintf(buf, sizeof(buf), "  %-22s %10s %10s %10s %10s %10s %10s", "", "p50", "p99",
                 "p99.9", "p99.99", "min", "max");
        std::string hdr_line = buf;

        std::string sep = "  ";
        for (int i = 0; i < 88; i++) sep += "\xe2\x94\x80";

        std::string full = title_bar + hdr_line + "\n" + sep + "\n";
        printf("%s", full.c_str());
        output += full;
    }

    void row(const char* label, Histogram& hist) {
        combined.add(hist);
        total_ops += hist.len();

        std::string name = current_section + " \xe2\x80\x94 " + label;
        histograms.emplace_back(name, hist);

        char buf[256];
        snprintf(buf, sizeof(buf), "  %-22s %10s %10s %10s %10s %10s %10s", label,
                 fmt_ns(hist.percentile(50.0)).c_str(), fmt_ns(hist.percentile(99.0)).c_str(),
                 fmt_ns(hist.percentile(99.9)).c_str(), fmt_ns(hist.percentile(99.99)).c_str(),
                 fmt_ns(hist.min_val()).c_str(), fmt_ns(hist.max_val()).c_str());
        printf("%s\n", buf);
        output += buf;
        output += '\n';
    }

    void summary(double elapsed_s) {
        auto throughput = static_cast<uint64_t>(static_cast<double>(total_ops) / elapsed_s);
        char buf[512];

        std::string text = "\n\xe2\x94\x80\xe2\x94\x80 Summary ";
        for (int i = 0; i < 51; i++) text += "\xe2\x94\x80";
        text += "\n";

        snprintf(buf, sizeof(buf), "  Total ops:          %s\n", fmt_ns(total_ops).c_str());
        text += buf;
        snprintf(buf, sizeof(buf), "  Throughput:         %s ops/sec\n",
                 fmt_ns(throughput).c_str());
        text += buf;
        snprintf(buf, sizeof(buf),
                 "  Latency (all ops):  p50=%s ns  p99=%s ns  p99.9=%s ns  max=%s ns\n",
                 fmt_ns(combined.percentile(50.0)).c_str(),
                 fmt_ns(combined.percentile(99.0)).c_str(),
                 fmt_ns(combined.percentile(99.9)).c_str(), fmt_ns(combined.max_val()).c_str());
        text += buf;
        snprintf(buf, sizeof(buf), "  Benchmark time:     %.2fs\n", elapsed_s);
        text += buf;

        printf("%s", text.c_str());
        output += text;
    }

    void save() const {
        std::string dir = results_dir();
        mkdirs(dir);

        std::string ts   = fmt_utc_timestamp();
        std::string path = dir + "/" + ts + ".txt";

        FILE* f = fopen(path.c_str(), "w");
        if (f == nullptr) {
            fprintf(stderr, "  Warning: could not save results to %s\n", path.c_str());
            return;
        }
        fwrite(output.data(), 1, output.size(), f);
        fclose(f);
        printf("  Results saved to %s\n", path.c_str());

        std::string hist_dir = dir + "/" + ts + "_histograms";
        mkdirs(hist_dir);

        for (const auto& [label, hist] : histograms) {
            std::string safe_name;
            for (char c : label) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_')
                    safe_name += c;
                else
                    safe_name += '_';
            }
            std::string csv = "percentile,latency_ns\n";
            Histogram   h_copy = hist;
            for (int p = 0; p <= 90; p++) {
                char line[64];
                snprintf(line, sizeof(line), "%.4f,%" PRIu64 "\n", static_cast<double>(p),
                         h_copy.percentile(static_cast<double>(p)));
                csv += line;
            }
            for (int p = 900; p <= 990; p++) {
                char line[64];
                snprintf(line, sizeof(line), "%.4f,%" PRIu64 "\n", static_cast<double>(p) / 10.0,
                         h_copy.percentile(static_cast<double>(p) / 10.0));
                csv += line;
            }
            for (int p = 9900; p <= 9990; p++) {
                char line[64];
                snprintf(line, sizeof(line), "%.4f,%" PRIu64 "\n", static_cast<double>(p) / 100.0,
                         h_copy.percentile(static_cast<double>(p) / 100.0));
                csv += line;
            }
            for (int p = 99900; p <= 99990; p++) {
                char line[64];
                snprintf(line, sizeof(line), "%.4f,%" PRIu64 "\n",
                         static_cast<double>(p) / 1000.0,
                         h_copy.percentile(static_cast<double>(p) / 1000.0));
                csv += line;
            }
            {
                char line[64];
                snprintf(line, sizeof(line), "99.9990,%" PRIu64 "\n",
                         h_copy.percentile(99.999));
                csv += line;
                snprintf(line, sizeof(line), "100.0000,%" PRIu64 "\n", h_copy.max_val());
                csv += line;
            }

            std::string csv_path = hist_dir + "/" + safe_name + ".csv";
            FILE*       cf = fopen(csv_path.c_str(), "w");
            if (cf != nullptr) {
                fwrite(csv.data(), 1, csv.size(), cf);
                fclose(cf);
            }
        }
        printf("  Histograms saved to %s/\n", hist_dir.c_str());
    }
};

// ── Seeding helpers (same logic as Rust) ────────────────────────────────────

static void seed_one_side(OrderBook& book, Side side, uint64_t n, uint64_t& id,
                          std::vector<Fill>& fills) {
    for (uint64_t i = 0; i < n; i++) {
        Price price = (side == Side::Buy) ? MID - SPREAD - (i % 200) : MID + SPREAD + (i % 200);
        fills.clear();
        book.add_order(Order{id, side, price, 10, OrderType::Limit}, fills);
        id++;
    }
}

static void seed_both(OrderBook& book, uint64_t n, uint64_t& id, std::vector<Fill>& fills) {
    for (uint64_t i = 0; i < n; i++) {
        Side  side  = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = (i % 2 == 0) ? MID - SPREAD - (i % 200) : MID + SPREAD + (i % 200);
        fills.clear();
        book.add_order(Order{id, side, price, 10, OrderType::Limit}, fills);
        id++;
    }
}

struct FreshBook {
    OrderBook         book;
    uint64_t          next_id;
    std::vector<Fill> fills;
};

static FreshBook fresh_book_both(uint64_t depth) {
    FreshBook fb;
    fb.fills.reserve(4);
    fb.next_id = 1;
    seed_both(fb.book, depth, fb.next_id, fb.fills);
    return fb;
}

static FreshBook fresh_book_asks(uint64_t depth) {
    FreshBook fb;
    fb.fills.reserve(4);
    fb.next_id = 1;
    seed_one_side(fb.book, Side::Sell, depth, fb.next_id, fb.fills);
    return fb;
}

// ── Scenarios (same logic as Rust) ──────────────────────────────────────────

static Histogram passive_insert(uint64_t depth) {
    OrderBook         book;
    std::vector<Fill> fills;
    fills.reserve(4);
    uint64_t id = 1;
    seed_both(book, depth, id, fills);

    Histogram hist;
    hist.reserve(ITERS);

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        Side  side = (id % 2 == 0) ? Side::Buy : Side::Sell;
        Price price =
            (id % 2 == 0) ? MID - SPREAD - 200 - (id % 100) : MID + SPREAD + 200 + (id % 100);
        fills.clear();

        uint64_t t0 = nts::instrument::raw_ticks();
        book.add_order(Order{id, side, price, 10, OrderType::Limit}, fills);
        uint64_t t1 = nts::instrument::raw_ticks();

        if (i >= WARMUP) hist.record(nts::instrument::ticks_to_ns(t1 - t0));
        id++;
    }
    return hist;
}

static Histogram aggressive_fill(uint64_t depth) {
    auto      fb        = fresh_book_asks(depth);
    size_t    refill_at = std::max(depth / 4, uint64_t(10));
    Histogram hist;
    hist.reserve(ITERS);

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        if (fb.book.len() < refill_at) fb = fresh_book_asks(depth);
        fb.fills.clear();

        uint64_t t0 = nts::instrument::raw_ticks();
        fb.book.add_order(Order{fb.next_id, Side::Buy, MID + SPREAD + 200, 1, OrderType::Limit},
                          fb.fills);
        uint64_t t1 = nts::instrument::raw_ticks();

        if (i >= WARMUP) hist.record(nts::instrument::ticks_to_ns(t1 - t0));
        fb.next_id++;
    }
    return hist;
}

static Histogram multi_level_sweep(uint64_t num_levels) {
    std::vector<Fill> fills;
    fills.reserve(num_levels);
    uint64_t  id = 1;
    Histogram hist;
    hist.reserve(SWEEP_ITERS);

    for (uint64_t i = 0; i < WARMUP + SWEEP_ITERS; i++) {
        OrderBook book;
        for (uint64_t l = 0; l < num_levels; l++) {
            fills.clear();
            book.add_order(Order{id, Side::Sell, MID + 1 + l, 10, OrderType::Limit}, fills);
            id++;
        }
        fills.clear();

        uint64_t t0 = nts::instrument::raw_ticks();
        book.add_order(Order{id, Side::Buy, MID + num_levels, num_levels * 10, OrderType::Limit},
                       fills);
        uint64_t t1 = nts::instrument::raw_ticks();

        if (i >= WARMUP) hist.record(nts::instrument::ticks_to_ns(t1 - t0));
        id++;
    }
    return hist;
}

static Histogram market_order(uint64_t depth) {
    auto      fb        = fresh_book_asks(depth);
    size_t    refill_at = std::max(depth / 4, uint64_t(10));
    Histogram hist;
    hist.reserve(ITERS);

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        if (fb.book.len() < refill_at) fb = fresh_book_asks(depth);
        fb.fills.clear();

        uint64_t t0 = nts::instrument::raw_ticks();
        fb.book.add_order(Order{fb.next_id, Side::Buy, 0, 1, OrderType::Market}, fb.fills);
        uint64_t t1 = nts::instrument::raw_ticks();

        if (i >= WARMUP) hist.record(nts::instrument::ticks_to_ns(t1 - t0));
        fb.next_id++;
    }
    return hist;
}

static Histogram cancel_bench(uint64_t depth) {
    auto      fb        = fresh_book_both(depth);
    uint64_t  cancel_id = fb.next_id - depth;
    Histogram hist;
    hist.reserve(ITERS);

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        if (cancel_id >= fb.next_id) {
            fb        = fresh_book_both(depth);
            cancel_id = fb.next_id - depth;
        }

        uint64_t t0 = nts::instrument::raw_ticks();
        fb.book.cancel(cancel_id);
        uint64_t t1 = nts::instrument::raw_ticks();

        if (i >= WARMUP) hist.record(nts::instrument::ticks_to_ns(t1 - t0));
        cancel_id++;
    }
    return hist;
}

static Histogram cancel_hot_level(uint64_t orders_per_level) {
    std::vector<Fill> fills;
    fills.reserve(4);
    uint64_t  id    = 1;
    Price     price = MID + SPREAD;
    uint64_t  iters = std::min(ITERS, orders_per_level);
    Histogram hist;
    hist.reserve(iters);

    OrderBook book;

    auto seed = [&]() -> uint64_t {
        uint64_t fid = id;
        for (uint64_t j = 0; j < orders_per_level; j++) {
            fills.clear();
            book.add_order(Order{id, Side::Sell, price, 10, OrderType::Limit}, fills);
            id++;
        }
        return fid;
    };

    uint64_t cancel_id = seed();

    for (uint64_t i = 0; i < WARMUP + iters; i++) {
        if (cancel_id >= id) {
            book      = OrderBook();
            cancel_id = seed();
        }

        uint64_t t0 = nts::instrument::raw_ticks();
        book.cancel(cancel_id);
        uint64_t t1 = nts::instrument::raw_ticks();

        if (i >= WARMUP) hist.record(nts::instrument::ticks_to_ns(t1 - t0));
        cancel_id++;
    }
    return hist;
}

static Histogram drain_single_level(uint64_t orders) {
    std::vector<Fill> fills;
    fills.reserve(orders);
    uint64_t  id    = 1;
    Price     price = MID + SPREAD;
    Histogram hist;
    hist.reserve(SWEEP_ITERS);

    for (uint64_t i = 0; i < WARMUP + SWEEP_ITERS; i++) {
        OrderBook book;
        for (uint64_t j = 0; j < orders; j++) {
            fills.clear();
            book.add_order(Order{id, Side::Sell, price, 1, OrderType::Limit}, fills);
            id++;
        }
        fills.clear();

        uint64_t t0 = nts::instrument::raw_ticks();
        book.add_order(Order{id, Side::Buy, price, orders, OrderType::Limit}, fills);
        uint64_t t1 = nts::instrument::raw_ticks();

        if (i >= WARMUP) hist.record(nts::instrument::ticks_to_ns(t1 - t0));
        id++;
    }
    return hist;
}

static Histogram mixed_workload(uint64_t depth) {
    std::vector<Fill> fills;
    fills.reserve(8);
    uint64_t  id = 1;
    OrderBook book;
    seed_both(book, depth, id, fills);

    size_t                ring_cap = std::max(depth, uint64_t(4096));
    std::vector<uint64_t> cancel_ring;
    cancel_ring.reserve(ring_cap);
    for (uint64_t i = 1; i <= depth; i++) cancel_ring.push_back(i);
    size_t ring_idx = 0;

    Histogram hist;
    hist.reserve(ITERS);

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        if (book.len() < 50) {
            book = OrderBook();
            id   = 1;
            seed_both(book, depth, id, fills);
            cancel_ring.clear();
            for (uint64_t j = 1; j <= depth; j++) cancel_ring.push_back(j);
            ring_idx = 0;
        }

        uint64_t roll = id % 20;

        uint64_t t0 = nts::instrument::raw_ticks();

        if (roll < 13) {
            if (!cancel_ring.empty()) {
                uint64_t cid = cancel_ring[ring_idx % cancel_ring.size()];
                book.cancel(cid);
                ring_idx++;
            }
        } else if (roll < 18) {
            Side  side = (id % 2 == 0) ? Side::Buy : Side::Sell;
            Price price =
                (id % 2 == 0) ? MID - SPREAD - 200 - (id % 100) : MID + SPREAD + 200 + (id % 100);
            fills.clear();
            book.add_order(Order{id, side, price, 10, OrderType::Limit}, fills);
            if (cancel_ring.size() < ring_cap) {
                cancel_ring.push_back(id);
            } else {
                cancel_ring[ring_idx % ring_cap] = id;
            }
        } else {
            Side  side  = (id % 2 == 0) ? Side::Buy : Side::Sell;
            Price price = (id % 2 == 0) ? MID + SPREAD + 200 : MID - SPREAD - 200;
            fills.clear();
            book.add_order(Order{id, side, price, 1, OrderType::Limit}, fills);
        }

        uint64_t t1 = nts::instrument::raw_ticks();
        if (i >= WARMUP) hist.record(nts::instrument::ticks_to_ns(t1 - t0));
        id++;
    }
    return hist;
}

// ── Correctness tests ───────────────────────────────────────────────────────

static Order limit(uint64_t id, Side side, uint64_t price, uint64_t qty) {
    return Order{id, side, price, qty, OrderType::Limit};
}
static Order market(uint64_t id, Side side, uint64_t qty) {
    return Order{id, side, 0, qty, OrderType::Market};
}

static void run_tests() {
    // test_no_match_wide_spread
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Buy, 100, 10), f);
        assert(f.empty());
        b.add_order(limit(2, Side::Sell, 110, 10), f);
        assert(f.empty());
        assert(b.best_bid() == 100);
        assert(b.best_ask() == 110);
        assert(b.spread() == 10);
        assert(b.len() == 2);
    }

    // test_exact_fill
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Sell, 100, 10), f);
        b.add_order(limit(2, Side::Buy, 100, 10), f);
        assert(f.size() == 1 && f[0].maker_id == 1 && f[0].qty == 10);
        assert(b.is_empty());
    }

    // test_partial_fill
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Sell, 100, 20), f);
        f.clear();
        b.add_order(limit(2, Side::Buy, 100, 5), f);
        assert(f.size() == 1 && f[0].qty == 5);
        assert(b.depth_at(Side::Sell, 100) == 15);
    }

    // test_price_improvement
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Sell, 95, 10), f);
        f.clear();
        b.add_order(limit(2, Side::Buy, 100, 10), f);
        assert(f[0].price == 95);
    }

    // test_fifo_priority
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Sell, 100, 5), f);
        b.add_order(limit(2, Side::Sell, 100, 5), f);
        f.clear();
        b.add_order(limit(3, Side::Buy, 100, 7), f);
        assert(f.size() == 2 && f[0].maker_id == 1 && f[1].maker_id == 2);
        assert(f[0].qty == 5 && f[1].qty == 2);
        assert(b.depth_at(Side::Sell, 100) == 3);
    }

    // test_multi_level_fill
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Sell, 100, 5), f);
        b.add_order(limit(2, Side::Sell, 101, 5), f);
        b.add_order(limit(3, Side::Sell, 102, 5), f);
        f.clear();
        b.add_order(limit(4, Side::Buy, 102, 12), f);
        assert(f.size() == 3);
        assert(f[0].price == 100 && f[1].price == 101 && f[2].price == 102);
        assert(f[2].qty == 2 && b.depth_at(Side::Sell, 102) == 3);
    }

    // test_market_order
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Sell, 100, 10), f);
        b.add_order(limit(2, Side::Sell, 105, 10), f);
        f.clear();
        b.add_order(market(3, Side::Buy, 15), f);
        assert(f.size() == 2 && f[0].qty == 10 && f[1].qty == 5);
        assert(b.len() == 1);
    }

    // test_cancel
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Buy, 100, 10), f);
        assert(b.cancel(1) && b.len() == 0 && !b.best_bid().has_value());
        assert(!b.cancel(1));
    }

    // test_cancel_nonexistent
    {
        OrderBook b;
        assert(!b.cancel(999));
    }

    // test_sell_market
    {
        OrderBook         b;
        std::vector<Fill> f;
        b.add_order(limit(1, Side::Buy, 100, 10), f);
        b.add_order(limit(2, Side::Buy, 99, 10), f);
        f.clear();
        b.add_order(market(3, Side::Sell, 15), f);
        assert(f.size() == 2 && f[0].price == 100 && f[1].price == 99);
    }

    fprintf(stderr, "All 10 tests passed.\n\n");
}

// ── Profile versions (no timing, pure workload for perf/flamegraph) ─────────

static void profile_passive_insert(uint64_t depth) {
    OrderBook         book;
    std::vector<Fill> fills;
    fills.reserve(4);
    uint64_t id = 1;
    seed_both(book, depth, id, fills);

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        Side  side  = (id % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = (id % 2 == 0) ? MID - SPREAD - 200 - (id % 100)
                                    : MID + SPREAD + 200 + (id % 100);
        fills.clear();
        book.add_order(Order{id, side, price, 10, OrderType::Limit}, fills);
        id++;
    }
}

static void profile_aggressive_fill(uint64_t depth) {
    auto   fb        = fresh_book_asks(depth);
    size_t refill_at = std::max(depth / 4, uint64_t(10));

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        if (fb.book.len() < refill_at) fb = fresh_book_asks(depth);
        fb.fills.clear();
        fb.book.add_order(Order{fb.next_id, Side::Buy, MID + SPREAD + 200, 1, OrderType::Limit},
                          fb.fills);
        fb.next_id++;
    }
}

static void profile_multi_level_sweep(uint64_t num_levels) {
    std::vector<Fill> fills;
    fills.reserve(num_levels);
    uint64_t id = 1;

    for (uint64_t i = 0; i < WARMUP + SWEEP_ITERS; i++) {
        OrderBook book;
        for (uint64_t l = 0; l < num_levels; l++) {
            fills.clear();
            book.add_order(Order{id, Side::Sell, MID + 1 + l, 10, OrderType::Limit}, fills);
            id++;
        }
        fills.clear();
        book.add_order(Order{id, Side::Buy, MID + num_levels, num_levels * 10, OrderType::Limit},
                       fills);
        id++;
    }
}

static void profile_market_order(uint64_t depth) {
    auto   fb        = fresh_book_asks(depth);
    size_t refill_at = std::max(depth / 4, uint64_t(10));

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        if (fb.book.len() < refill_at) fb = fresh_book_asks(depth);
        fb.fills.clear();
        fb.book.add_order(Order{fb.next_id, Side::Buy, 0, 1, OrderType::Market}, fb.fills);
        fb.next_id++;
    }
}

static void profile_cancel(uint64_t depth) {
    auto     fb        = fresh_book_both(depth);
    uint64_t cancel_id = fb.next_id - depth;

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        if (cancel_id >= fb.next_id) {
            fb        = fresh_book_both(depth);
            cancel_id = fb.next_id - depth;
        }
        fb.book.cancel(cancel_id);
        cancel_id++;
    }
}

static void profile_cancel_hot_level(uint64_t orders_per_level) {
    std::vector<Fill> fills;
    fills.reserve(4);
    uint64_t  id    = 1;
    Price     price = MID + SPREAD;
    uint64_t  iters = std::min(ITERS, orders_per_level);
    OrderBook book;

    auto seed = [&]() -> uint64_t {
        uint64_t fid = id;
        for (uint64_t j = 0; j < orders_per_level; j++) {
            fills.clear();
            book.add_order(Order{id, Side::Sell, price, 10, OrderType::Limit}, fills);
            id++;
        }
        return fid;
    };

    uint64_t cancel_id = seed();
    for (uint64_t i = 0; i < WARMUP + iters; i++) {
        if (cancel_id >= id) {
            book      = OrderBook();
            cancel_id = seed();
        }
        book.cancel(cancel_id);
        cancel_id++;
    }
}

static void profile_drain_single_level(uint64_t orders) {
    std::vector<Fill> fills;
    fills.reserve(orders);
    uint64_t id    = 1;
    Price    price = MID + SPREAD;

    for (uint64_t i = 0; i < WARMUP + SWEEP_ITERS; i++) {
        OrderBook book;
        for (uint64_t j = 0; j < orders; j++) {
            fills.clear();
            book.add_order(Order{id, Side::Sell, price, 1, OrderType::Limit}, fills);
            id++;
        }
        fills.clear();
        book.add_order(Order{id, Side::Buy, price, orders, OrderType::Limit}, fills);
        id++;
    }
}

static void profile_mixed_workload(uint64_t depth) {
    std::vector<Fill> fills;
    fills.reserve(8);
    uint64_t  id = 1;
    OrderBook book;
    seed_both(book, depth, id, fills);

    size_t                ring_cap = std::max(depth, uint64_t(4096));
    std::vector<uint64_t> cancel_ring;
    cancel_ring.reserve(ring_cap);
    for (uint64_t i = 1; i <= depth; i++) cancel_ring.push_back(i);
    size_t ring_idx = 0;

    for (uint64_t i = 0; i < WARMUP + ITERS; i++) {
        if (book.len() < 50) {
            book = OrderBook();
            id   = 1;
            seed_both(book, depth, id, fills);
            cancel_ring.clear();
            for (uint64_t j = 1; j <= depth; j++) cancel_ring.push_back(j);
            ring_idx = 0;
        }
        uint64_t roll = id % 20;
        if (roll < 13) {
            if (!cancel_ring.empty()) {
                book.cancel(cancel_ring[ring_idx % cancel_ring.size()]);
                ring_idx++;
            }
        } else if (roll < 18) {
            Side  side = (id % 2 == 0) ? Side::Buy : Side::Sell;
            Price price =
                (id % 2 == 0) ? MID - SPREAD - 200 - (id % 100) : MID + SPREAD + 200 + (id % 100);
            fills.clear();
            book.add_order(Order{id, side, price, 10, OrderType::Limit}, fills);
            if (cancel_ring.size() < ring_cap)
                cancel_ring.push_back(id);
            else
                cancel_ring[ring_idx % ring_cap] = id;
        } else {
            Side  side  = (id % 2 == 0) ? Side::Buy : Side::Sell;
            Price price = (id % 2 == 0) ? MID + SPREAD + 200 : MID - SPREAD - 200;
            fills.clear();
            book.add_order(Order{id, side, price, 1, OrderType::Limit}, fills);
        }
        id++;
    }
}

// ── CLI helpers ─────────────────────────────────────────────────────────────

enum class ScenarioKind {
    PassiveInsert,
    AggressiveFill,
    MultiLevelSweep,
    MarketOrder,
    Cancel,
    CancelHotLevel,
    DrainSingleLevel,
    MixedWorkload,
};

static bool parse_scenario(const char* name, ScenarioKind& out) {
    struct Entry { const char* name; ScenarioKind kind; };
    static const Entry table[] = {
        {"passive-insert",    ScenarioKind::PassiveInsert},
        {"aggressive-fill",   ScenarioKind::AggressiveFill},
        {"multi-level-sweep", ScenarioKind::MultiLevelSweep},
        {"market-order",      ScenarioKind::MarketOrder},
        {"cancel",            ScenarioKind::Cancel},
        {"cancel-hot-level",  ScenarioKind::CancelHotLevel},
        {"drain-single-level",ScenarioKind::DrainSingleLevel},
        {"mixed-workload",    ScenarioKind::MixedWorkload},
    };
    for (const auto& e : table) {
        if (strcmp(name, e.name) == 0) { out = e.kind; return true; }
    }
    return false;
}

static std::vector<uint64_t> values(uint64_t custom, const std::vector<uint64_t>& defaults) {
    return (custom != 0) ? std::vector<uint64_t>{custom} : defaults;
}

static void run_bench_scenario(Reporter& r, ScenarioKind kind, uint64_t depth, uint64_t levels,
                               uint64_t orders) {
    char label[32];
    switch (kind) {
        case ScenarioKind::PassiveInsert:
            r.section("Passive Insert");
            for (uint64_t d : values(depth, {0, 100, 10'000, 100'000})) {
                auto h = passive_insert(d);
                snprintf(label, sizeof(label), "depth=%s", fmt_depth(d).c_str());
                r.row(label, h);
            }
            break;
        case ScenarioKind::AggressiveFill:
            r.section("Aggressive Fill (1 lot)");
            for (uint64_t d : values(depth, {100, 10'000, 100'000})) {
                auto h = aggressive_fill(d);
                snprintf(label, sizeof(label), "depth=%s", fmt_depth(d).c_str());
                r.row(label, h);
            }
            break;
        case ScenarioKind::MultiLevelSweep:
            r.section("Multi-Level Sweep");
            for (uint64_t l : values(levels, {1, 5, 10, 50})) {
                auto h = multi_level_sweep(l);
                snprintf(label, sizeof(label), "%llu levels", (unsigned long long)l);
                r.row(label, h);
            }
            break;
        case ScenarioKind::MarketOrder:
            r.section("Market Order (1 lot)");
            for (uint64_t d : values(depth, {100, 10'000, 100'000})) {
                auto h = market_order(d);
                snprintf(label, sizeof(label), "depth=%s", fmt_depth(d).c_str());
                r.row(label, h);
            }
            break;
        case ScenarioKind::Cancel:
            r.section("Cancel");
            for (uint64_t d : values(depth, {100, 10'000, 100'000})) {
                auto h = cancel_bench(d);
                snprintf(label, sizeof(label), "depth=%s", fmt_depth(d).c_str());
                r.row(label, h);
            }
            break;
        case ScenarioKind::CancelHotLevel:
            r.section("Cancel Hot Level (single price)");
            for (uint64_t n : values(orders, {10, 100, 1'000, 10'000})) {
                auto h = cancel_hot_level(n);
                snprintf(label, sizeof(label), "%llu orders/level", (unsigned long long)n);
                r.row(label, h);
            }
            break;
        case ScenarioKind::DrainSingleLevel:
            r.section("Drain Single Level");
            for (uint64_t n : values(orders, {10, 50, 100, 500, 1'000})) {
                auto h = drain_single_level(n);
                snprintf(label, sizeof(label), "%llu orders", (unsigned long long)n);
                r.row(label, h);
            }
            break;
        case ScenarioKind::MixedWorkload:
            r.section("Mixed Workload (65% cancel, 25% insert, 10% fill)");
            for (uint64_t d : values(depth, {100, 10'000, 100'000})) {
                auto h = mixed_workload(d);
                snprintf(label, sizeof(label), "depth=%s", fmt_depth(d).c_str());
                r.row(label, h);
            }
            break;
    }
}

static void run_profile_scenario(ScenarioKind kind, uint64_t depth, uint64_t levels,
                                 uint64_t orders, uint64_t repeat) {
    for (uint64_t rep = 0; rep < repeat; rep++) {
        switch (kind) {
            case ScenarioKind::PassiveInsert:
                for (uint64_t d : values(depth, {0, 100, 10'000, 100'000}))
                    profile_passive_insert(d);
                break;
            case ScenarioKind::AggressiveFill:
                for (uint64_t d : values(depth, {100, 10'000, 100'000}))
                    profile_aggressive_fill(d);
                break;
            case ScenarioKind::MultiLevelSweep:
                for (uint64_t l : values(levels, {1, 5, 10, 50}))
                    profile_multi_level_sweep(l);
                break;
            case ScenarioKind::MarketOrder:
                for (uint64_t d : values(depth, {100, 10'000, 100'000}))
                    profile_market_order(d);
                break;
            case ScenarioKind::Cancel:
                for (uint64_t d : values(depth, {100, 10'000, 100'000}))
                    profile_cancel(d);
                break;
            case ScenarioKind::CancelHotLevel:
                for (uint64_t n : values(orders, {10, 100, 1'000, 10'000}))
                    profile_cancel_hot_level(n);
                break;
            case ScenarioKind::DrainSingleLevel:
                for (uint64_t n : values(orders, {10, 50, 100, 500, 1'000}))
                    profile_drain_single_level(n);
                break;
            case ScenarioKind::MixedWorkload:
                for (uint64_t d : values(depth, {100, 10'000, 100'000}))
                    profile_mixed_workload(d);
                break;
        }
    }
}

// ── Main ────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s                                          Run all benchmarks\n"
        "  %s bench [--scenario <name>] [--depth N] [--levels N] [--orders N]\n"
        "  %s profile --scenario <name> [--depth N] [--levels N] [--orders N] [--repeat N]\n"
        "\n"
        "Scenarios: passive-insert, aggressive-fill, multi-level-sweep, market-order,\n"
        "           cancel, cancel-hot-level, drain-single-level, mixed-workload\n",
        prog, prog, prog);
}

int main(int argc, char* argv[]) {
    run_tests();

    enum class Mode { BenchAll, BenchScenario, Profile };
    Mode         mode     = Mode::BenchAll;
    ScenarioKind scenario = ScenarioKind::PassiveInsert;
    uint64_t     depth    = 0;
    uint64_t     levels   = 0;
    uint64_t     orders   = 0;
    uint64_t     repeat   = 1;
    bool         has_scenario = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "bench") == 0) {
            mode = Mode::BenchAll;
        } else if (strcmp(argv[i], "profile") == 0) {
            mode = Mode::Profile;
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            if (!parse_scenario(argv[++i], scenario)) {
                fprintf(stderr, "Unknown scenario: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
            has_scenario = true;
            if (mode == Mode::BenchAll) mode = Mode::BenchScenario;
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            depth = strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--levels") == 0 && i + 1 < argc) {
            levels = strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--orders") == 0 && i + 1 < argc) {
            orders = strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            repeat = strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (mode == Mode::Profile) {
        if (!has_scenario) {
            fprintf(stderr, "profile mode requires --scenario\n");
            print_usage(argv[0]);
            return 1;
        }
        uint64_t t0 = nts::instrument::now_ns();
        run_profile_scenario(scenario, depth, levels, orders, repeat);
        double elapsed_s = static_cast<double>(nts::instrument::now_ns() - t0) / 1'000'000'000.0;
        fprintf(stderr, "profile complete: repeat=%llu elapsed=%.2fs\n",
                (unsigned long long)repeat, elapsed_s);
        return 0;
    }

    Reporter r;
    r.header("=== Matching Engine Latency Benchmark (C++) ===");
    r.git_version();

    char params[128];
    snprintf(params, sizeof(params), "    warmup=%llu  iters=%llu  sweep_iters=%llu",
             (unsigned long long)WARMUP, (unsigned long long)ITERS,
             (unsigned long long)SWEEP_ITERS);
    r.header(params);

    uint64_t t0 = nts::instrument::now_ns();

    if (mode == Mode::BenchScenario) {
        run_bench_scenario(r, scenario, depth, levels, orders);
    } else {
        static const ScenarioKind all_scenarios[] = {
            ScenarioKind::PassiveInsert,   ScenarioKind::AggressiveFill,
            ScenarioKind::MultiLevelSweep, ScenarioKind::MarketOrder,
            ScenarioKind::Cancel,          ScenarioKind::CancelHotLevel,
            ScenarioKind::DrainSingleLevel,ScenarioKind::MixedWorkload,
        };
        for (auto s : all_scenarios) run_bench_scenario(r, s, 0, 0, 0);
    }

    double elapsed_s = static_cast<double>(nts::instrument::now_ns() - t0) / 1'000'000'000.0;
    r.summary(elapsed_s);
    r.save();

    return 0;
}
