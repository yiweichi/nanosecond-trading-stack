#include "nts/pipeline_utils.h"

#include <sys/stat.h>

#include <cstring>
#include <ctime>
#include <string>

namespace nts {

void OrderTickRing::set(nts::OrderId id, uint64_t ticks) {
    Entry& entry = entries_[index(id)];
    if (entry.occupied && entry.id != id) collisions_++;
    entry = Entry{id, ticks, true};
}

bool OrderTickRing::get(nts::OrderId id, uint64_t& ticks) const {
    const Entry& entry = entries_[index(id)];
    if (!entry.occupied || entry.id != id) return false;
    ticks = entry.ticks;
    return true;
}

void OrderTickRing::erase(nts::OrderId id) {
    Entry& entry = entries_[index(id)];
    if (entry.occupied && entry.id == id) entry.occupied = false;
}

size_t OrderTickRing::collisions() const {
    return collisions_;
}

Args parse_args(int argc, char* argv[]) {
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

const char* live_results_dir() {
#if defined(__APPLE__)
    return "results/live/mac";
#elif defined(__linux__)
    return "results/live/linux";
#else
    return "results/live/other";
#endif
}

void mkdirs(const std::string& path) {
    std::string partial;
    for (char c : path) {
        partial += c;
        if (c == '/') mkdir(partial.c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
}

std::string utc_timestamp() {
    time_t    now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char ts[32];
    snprintf(ts, sizeof(ts), "%04d%02d%02dT%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1,
             t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return ts;
}

void print_order_tick_ring_report(const char* name, const OrderTickRing& ring, FILE* out) {
    fprintf(out, "[profile] %s: collisions=%zu\n", name, ring.collisions());
}

void print_trading_report(const nts::MdReceiver& ref_md, const nts::MdReceiver& target_md,
                          nts::OMS& oms, const nts::OrderBook& book, double elapsed_s,
                          uint64_t iterations, FILE* out) {
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

}  // namespace nts
