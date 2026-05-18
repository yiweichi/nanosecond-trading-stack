#include "nts/perf_counter.h"

#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace nts::instrument {

#if defined(__linux__)
static long perf_event_open(perf_event_attr* hw_event, pid_t pid, int cpu, int group_fd,
                            unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}
#endif

[[maybe_unused]] static const char* perf_event_name(PerfEvent event) {
    switch (event) {
        case PerfEvent::CpuCycles: return "cycles";
        case PerfEvent::Instructions: return "instructions";
        case PerfEvent::CacheReferences: return "cache-references";
        case PerfEvent::CacheMisses: return "cache-misses";
        case PerfEvent::Branches: return "branches";
        case PerfEvent::BranchMisses: return "branch-misses";
        case PerfEvent::PageFaults: return "page-faults";
        case PerfEvent::L1DLoads: return "L1-dcache-loads";
        case PerfEvent::L1DLoadMisses: return "L1-dcache-load-misses";
        case PerfEvent::L1DStores: return "L1-dcache-stores";
        case PerfEvent::LLCLoads: return "LLC-loads";
        case PerfEvent::LLCLoadMisses: return "LLC-load-misses";
        case PerfEvent::LLCStores: return "LLC-stores";
        case PerfEvent::LLCStoreMisses: return "LLC-store-misses";
    }
    return "unknown";
}

PerfCounter::PerfCounter(PerfEvent event) {
    init(event);
}

PerfCounter::~PerfCounter() {
#if defined(__linux__)
    if (fd_ != -1) close(fd_);
#endif
}

void PerfCounter::init(PerfEvent event) {
#if defined(__linux__)
    perf_event_attr attr{};
    attr.size           = sizeof(perf_event_attr);
    attr.disabled       = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv     = 1;
    attr.inherit        = 0;

    switch (event) {
        case PerfEvent::CpuCycles:
            attr.type   = PERF_TYPE_HARDWARE;
            attr.config = PERF_COUNT_HW_CPU_CYCLES;
            break;
        case PerfEvent::Instructions:
            attr.type   = PERF_TYPE_HARDWARE;
            attr.config = PERF_COUNT_HW_INSTRUCTIONS;
            break;
        case PerfEvent::CacheReferences:
            attr.type   = PERF_TYPE_HARDWARE;
            attr.config = PERF_COUNT_HW_CACHE_REFERENCES;
            break;
        case PerfEvent::CacheMisses:
            attr.type   = PERF_TYPE_HARDWARE;
            attr.config = PERF_COUNT_HW_CACHE_MISSES;
            break;
        case PerfEvent::Branches:
            attr.type   = PERF_TYPE_HARDWARE;
            attr.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
            break;
        case PerfEvent::BranchMisses:
            attr.type   = PERF_TYPE_HARDWARE;
            attr.config = PERF_COUNT_HW_BRANCH_MISSES;
            break;
        case PerfEvent::PageFaults:
            attr.type   = PERF_TYPE_SOFTWARE;
            attr.config = PERF_COUNT_SW_PAGE_FAULTS;
            break;
        case PerfEvent::L1DLoads:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L1DLoadMisses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
        case PerfEvent::L1DStores:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::LLCLoads:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::LLCLoadMisses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
        case PerfEvent::LLCStores:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::LLCStoreMisses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
    }

    config_ = attr.config;

    fd_ = static_cast<int>(perf_event_open(&attr, 0, -1, -1, 0));
    if (fd_ == -1) {
        fprintf(stderr, "[pmu] unsupported event name=%s type=%u config=%llu error=%s\n",
                perf_event_name(event), attr.type, static_cast<unsigned long long>(attr.config),
                std::strerror(errno));
    }
#else
    config_ = static_cast<uint64_t>(event);
    fd_     = -1;
#endif
}

void PerfCounter::reset() const {
#if defined(__linux__)
    if (fd_ != -1) ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
#endif
}

void PerfCounter::enable() const {
#if defined(__linux__)
    if (fd_ != -1) ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
#endif
}

void PerfCounter::disable() const {
#if defined(__linux__)
    if (fd_ != -1) ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
#endif
}

uint64_t PerfCounter::read_value() const {
#if defined(__linux__)
    if (fd_ == -1) return 0;

    uint64_t      value = 0;
    const ssize_t n     = ::read(fd_, &value, sizeof(value));
    return (n == static_cast<ssize_t>(sizeof(value))) ? value : 0;
#else
    return 0;
#endif
}

static const char* pmu_group_name() {
    const char* group = std::getenv("NTS_PMU_GROUP");
    if (group == nullptr || group[0] == '\0') return "core";
    return group;
}

static bool pmu_group_is(const char* expected) {
    return std::strcmp(pmu_group_name(), expected) == 0;
}

static bool pmu_event_enabled(const char* event_group) {
    return pmu_group_is("all") || pmu_group_is(event_group);
}

static PerfCounter& pmu_cycles() {
    static PerfCounter counter(PerfEvent::CpuCycles);
    return counter;
}
static PerfCounter& pmu_instructions() {
    static PerfCounter counter(PerfEvent::Instructions);
    return counter;
}
static PerfCounter& pmu_cache_references() {
    static PerfCounter counter(PerfEvent::CacheReferences);
    return counter;
}
static PerfCounter& pmu_cache_misses() {
    static PerfCounter counter(PerfEvent::CacheMisses);
    return counter;
}
static PerfCounter& pmu_branches() {
    static PerfCounter counter(PerfEvent::Branches);
    return counter;
}
static PerfCounter& pmu_branch_misses() {
    static PerfCounter counter(PerfEvent::BranchMisses);
    return counter;
}
static PerfCounter& pmu_page_faults() {
    static PerfCounter counter(PerfEvent::PageFaults);
    return counter;
}
static PerfCounter& pmu_l1d_loads() {
    static PerfCounter counter(PerfEvent::L1DLoads);
    return counter;
}
static PerfCounter& pmu_l1d_load_misses() {
    static PerfCounter counter(PerfEvent::L1DLoadMisses);
    return counter;
}
static PerfCounter& pmu_l1d_stores() {
    static PerfCounter counter(PerfEvent::L1DStores);
    return counter;
}
static PerfCounter& pmu_llc_loads() {
    static PerfCounter counter(PerfEvent::LLCLoads);
    return counter;
}
static PerfCounter& pmu_llc_load_misses() {
    static PerfCounter counter(PerfEvent::LLCLoadMisses);
    return counter;
}
static PerfCounter& pmu_llc_stores() {
    static PerfCounter counter(PerfEvent::LLCStores);
    return counter;
}
static PerfCounter& pmu_llc_store_misses() {
    static PerfCounter counter(PerfEvent::LLCStoreMisses);
    return counter;
}

static void reset_enabled_pmu_counters() {
    if (pmu_event_enabled("core")) {
        pmu_cycles().reset();
        pmu_instructions().reset();
        pmu_branches().reset();
        pmu_branch_misses().reset();
        pmu_page_faults().reset();
    }
    if (pmu_event_enabled("cache")) {
        pmu_cache_references().reset();
        pmu_cache_misses().reset();
    }
    if (pmu_event_enabled("l1d")) {
        pmu_l1d_loads().reset();
        pmu_l1d_load_misses().reset();
        pmu_l1d_stores().reset();
    }
    if (pmu_event_enabled("llc")) {
        pmu_llc_loads().reset();
        pmu_llc_load_misses().reset();
        pmu_llc_stores().reset();
        pmu_llc_store_misses().reset();
    }
}

static void enable_enabled_pmu_counters() {
    if (pmu_event_enabled("core")) {
        pmu_cycles().enable();
        pmu_instructions().enable();
        pmu_branches().enable();
        pmu_branch_misses().enable();
        pmu_page_faults().enable();
    }
    if (pmu_event_enabled("cache")) {
        pmu_cache_references().enable();
        pmu_cache_misses().enable();
    }
    if (pmu_event_enabled("l1d")) {
        pmu_l1d_loads().enable();
        pmu_l1d_load_misses().enable();
        pmu_l1d_stores().enable();
    }
    if (pmu_event_enabled("llc")) {
        pmu_llc_loads().enable();
        pmu_llc_load_misses().enable();
        pmu_llc_stores().enable();
        pmu_llc_store_misses().enable();
    }
}

static void disable_enabled_pmu_counters() {
    if (pmu_event_enabled("core")) {
        pmu_cycles().disable();
        pmu_instructions().disable();
        pmu_branches().disable();
        pmu_branch_misses().disable();
        pmu_page_faults().disable();
    }
    if (pmu_event_enabled("cache")) {
        pmu_cache_references().disable();
        pmu_cache_misses().disable();
    }
    if (pmu_event_enabled("l1d")) {
        pmu_l1d_loads().disable();
        pmu_l1d_load_misses().disable();
        pmu_l1d_stores().disable();
    }
    if (pmu_event_enabled("llc")) {
        pmu_llc_loads().disable();
        pmu_llc_load_misses().disable();
        pmu_llc_stores().disable();
        pmu_llc_store_misses().disable();
    }
}

static void accumulate_enabled_pmu_counters(PmuProfileTotals& totals) {
    totals.calls++;
    if (pmu_event_enabled("core")) {
        totals.cycles += pmu_cycles().read_value();
        totals.instructions += pmu_instructions().read_value();
        totals.branches += pmu_branches().read_value();
        totals.branch_misses += pmu_branch_misses().read_value();
        totals.page_faults += pmu_page_faults().read_value();
    }
    if (pmu_event_enabled("cache")) {
        totals.cache_references += pmu_cache_references().read_value();
        totals.cache_misses += pmu_cache_misses().read_value();
    }
    if (pmu_event_enabled("l1d")) {
        totals.l1d_loads += pmu_l1d_loads().read_value();
        totals.l1d_load_misses += pmu_l1d_load_misses().read_value();
        totals.l1d_stores += pmu_l1d_stores().read_value();
    }
    if (pmu_event_enabled("llc")) {
        totals.llc_loads += pmu_llc_loads().read_value();
        totals.llc_load_misses += pmu_llc_load_misses().read_value();
        totals.llc_stores += pmu_llc_stores().read_value();
        totals.llc_store_misses += pmu_llc_store_misses().read_value();
    }
}

PmuProfileScope::PmuProfileScope(PmuProfileTotals& totals) : totals_(totals) {
    reset_enabled_pmu_counters();
    enable_enabled_pmu_counters();
}

PmuProfileScope::~PmuProfileScope() {
    disable_enabled_pmu_counters();
    accumulate_enabled_pmu_counters(totals_);
}

static double pmu_ratio(uint64_t numerator, uint64_t denominator) {
    return denominator == 0
               ? 0.0
               : 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

static double pmu_per_call(uint64_t value, uint64_t calls) {
    return calls == 0 ? 0.0 : static_cast<double>(value) / static_cast<double>(calls);
}

static void print_pmu_counter_line(FILE* out, uint64_t value, const char* event,
                                   const char* comment = nullptr) {
    if (comment != nullptr) {
        fprintf(out, "%18llu      %-28s # %s\n", static_cast<unsigned long long>(value), event,
                comment);
    } else {
        fprintf(out, "%18llu      %s\n", static_cast<unsigned long long>(value), event);
    }
}

void print_pmu_profile_totals(const PmuProfileTotals& totals, FILE* out) {
    const char* group = pmu_group_name();
    fprintf(out, "\n Performance counter stats for 'process_market_signal_and_order' ");
    fprintf(out, "(NTS_PMU_GROUP=%s):\n\n", group);
    print_pmu_counter_line(out, totals.calls, "calls");

    char comment[96];
    if (pmu_event_enabled("core")) {
        snprintf(comment, sizeof(comment), "%.2f cycles per call",
                 pmu_per_call(totals.cycles, totals.calls));
        print_pmu_counter_line(out, totals.cycles, "cycles", comment);

        snprintf(comment, sizeof(comment), "%.2f insn per cycle",
                 totals.cycles == 0 ? 0.0
                                    : static_cast<double>(totals.instructions) /
                                          static_cast<double>(totals.cycles));
        print_pmu_counter_line(out, totals.instructions, "instructions", comment);

        print_pmu_counter_line(out, totals.branches, "branches");
        snprintf(comment, sizeof(comment), "%.2f%% of all branches",
                 pmu_ratio(totals.branch_misses, totals.branches));
        print_pmu_counter_line(out, totals.branch_misses, "branch-misses", comment);
        print_pmu_counter_line(out, totals.page_faults, "page-faults");
    }

    if (pmu_event_enabled("cache")) {
        print_pmu_counter_line(out, totals.cache_references, "cache-references");
        snprintf(comment, sizeof(comment), "%.2f%% of all cache refs",
                 pmu_ratio(totals.cache_misses, totals.cache_references));
        print_pmu_counter_line(out, totals.cache_misses, "cache-misses", comment);
    }

    if (pmu_event_enabled("l1d")) {
        print_pmu_counter_line(out, totals.l1d_loads, "L1-dcache-loads");
        snprintf(comment, sizeof(comment), "%.2f%% of all L1D loads",
                 pmu_ratio(totals.l1d_load_misses, totals.l1d_loads));
        print_pmu_counter_line(out, totals.l1d_load_misses, "L1-dcache-load-misses", comment);
        print_pmu_counter_line(out, totals.l1d_stores, "L1-dcache-stores");
    }

    if (pmu_event_enabled("llc")) {
        print_pmu_counter_line(out, totals.llc_loads, "LLC-loads");
        snprintf(comment, sizeof(comment), "%.2f%% of all LLC loads",
                 pmu_ratio(totals.llc_load_misses, totals.llc_loads));
        print_pmu_counter_line(out, totals.llc_load_misses, "LLC-load-misses", comment);
        print_pmu_counter_line(out, totals.llc_stores, "LLC-stores");
        snprintf(comment, sizeof(comment), "%.2f%% of all LLC stores",
                 pmu_ratio(totals.llc_store_misses, totals.llc_stores));
        print_pmu_counter_line(out, totals.llc_store_misses, "LLC-store-misses", comment);
    }

    if (!pmu_group_is("core") && !pmu_group_is("cache") && !pmu_group_is("l1d") &&
        !pmu_group_is("llc") && !pmu_group_is("all")) {
        fprintf(out,
                "\n [pmu] unknown NTS_PMU_GROUP=%s; valid values: core, cache, l1d, llc, "
                "all. No counters were enabled.\n",
                group);
    }

    fprintf(out, "\n");
}

}  // namespace nts::instrument
