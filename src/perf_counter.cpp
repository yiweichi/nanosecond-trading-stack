#include "nts/perf_counter.h"

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace nts::instrument {

static long perf_event_open(perf_event_attr* hw_event, pid_t pid, int cpu, int group_fd,
                            unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static const char* perf_event_name(PerfEvent event) {
    switch (event) {
        case PerfEvent::CpuCycles: return "cycles";
        case PerfEvent::Instructions: return "instructions";
        case PerfEvent::CacheReferences: return "cache-references";
        case PerfEvent::CacheMisses: return "cache-misses";
        case PerfEvent::Branches: return "branches";
        case PerfEvent::BranchMisses: return "branch-misses";
        case PerfEvent::PageFaults: return "page-faults";
        case PerfEvent::L1DReferences: return "L1D-references";
        case PerfEvent::L1DMisses: return "L1D-misses";
        case PerfEvent::L1IReferences: return "L1I-references";
        case PerfEvent::L1IMisses: return "L1I-misses";
        case PerfEvent::L2References: return "L2-references";
        case PerfEvent::L2Misses: return "L2-misses";
        case PerfEvent::L3References: return "L3-references";
        case PerfEvent::L3Misses: return "L3-misses";
    }
    return "unknown";
}

PerfCounter::PerfCounter(PerfEvent event) {
    init(event);
}

PerfCounter::~PerfCounter() {
    if (fd_ != -1) close(fd_);
}

void PerfCounter::init(PerfEvent event) {
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
        case PerfEvent::L1DReferences:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L1DMisses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
        case PerfEvent::L1IReferences:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L1IMisses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
        case PerfEvent::L2References:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L2Misses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
        case PerfEvent::L3References:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L3Misses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
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
}

void PerfCounter::reset() const {
    if (fd_ != -1) ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
}

void PerfCounter::enable() const {
    if (fd_ != -1) ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
}

void PerfCounter::disable() const {
    if (fd_ != -1) ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
}

uint64_t PerfCounter::read_value() const {
    if (fd_ == -1) return 0;

    uint64_t      value = 0;
    const ssize_t n     = ::read(fd_, &value, sizeof(value));
    return (n == static_cast<ssize_t>(sizeof(value))) ? value : 0;
}

}  // namespace nts::instrument
#else
namespace nts::instrument {

PerfCounter::PerfCounter(PerfEvent event) {
    init(event);
}

PerfCounter::~PerfCounter() = default;

void PerfCounter::init(PerfEvent event) {
    config_ = static_cast<uint64_t>(event);
    fd_     = -1;
}

void     PerfCounter::reset() const {}
void     PerfCounter::enable() const {}
void     PerfCounter::disable() const {}
uint64_t PerfCounter::read_value() const {
    return 0;
}

}  // namespace nts::instrument
#endif

namespace nts::instrument {

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
static PerfCounter& pmu_l1d_references() {
    static PerfCounter counter(PerfEvent::L1DReferences);
    return counter;
}
static PerfCounter& pmu_l1d_misses() {
    static PerfCounter counter(PerfEvent::L1DMisses);
    return counter;
}
static PerfCounter& pmu_l1i_references() {
    static PerfCounter counter(PerfEvent::L1IReferences);
    return counter;
}
static PerfCounter& pmu_l1i_misses() {
    static PerfCounter counter(PerfEvent::L1IMisses);
    return counter;
}
static PerfCounter& pmu_l2_references() {
    static PerfCounter counter(PerfEvent::L2References);
    return counter;
}
static PerfCounter& pmu_l2_misses() {
    static PerfCounter counter(PerfEvent::L2Misses);
    return counter;
}
static PerfCounter& pmu_l3_references() {
    static PerfCounter counter(PerfEvent::L3References);
    return counter;
}
static PerfCounter& pmu_l3_misses() {
    static PerfCounter counter(PerfEvent::L3Misses);
    return counter;
}

PmuProfileScope::PmuProfileScope(PmuProfileTotals& totals) : totals_(totals) {
    pmu_cycles().reset();
    pmu_instructions().reset();
    pmu_cache_references().reset();
    pmu_cache_misses().reset();
    pmu_branches().reset();
    pmu_branch_misses().reset();
    pmu_page_faults().reset();
    pmu_l1d_references().reset();
    pmu_l1d_misses().reset();
    pmu_l1i_references().reset();
    pmu_l1i_misses().reset();
    pmu_l2_references().reset();
    pmu_l2_misses().reset();
    pmu_l3_references().reset();
    pmu_l3_misses().reset();

    pmu_cycles().enable();
    pmu_instructions().enable();
    pmu_cache_references().enable();
    pmu_cache_misses().enable();
    pmu_branches().enable();
    pmu_branch_misses().enable();
    pmu_page_faults().enable();
    pmu_l1d_references().enable();
    pmu_l1d_misses().enable();
    pmu_l1i_references().enable();
    pmu_l1i_misses().enable();
    pmu_l2_references().enable();
    pmu_l2_misses().enable();
    pmu_l3_references().enable();
    pmu_l3_misses().enable();
}

PmuProfileScope::~PmuProfileScope() {
    pmu_cycles().disable();
    pmu_instructions().disable();
    pmu_cache_references().disable();
    pmu_cache_misses().disable();
    pmu_branches().disable();
    pmu_branch_misses().disable();
    pmu_page_faults().disable();
    pmu_l1d_references().disable();
    pmu_l1d_misses().disable();
    pmu_l1i_references().disable();
    pmu_l1i_misses().disable();
    pmu_l2_references().disable();
    pmu_l2_misses().disable();
    pmu_l3_references().disable();
    pmu_l3_misses().disable();

    totals_.calls++;
    totals_.cycles += pmu_cycles().read_value();
    totals_.instructions += pmu_instructions().read_value();
    totals_.cache_references += pmu_cache_references().read_value();
    totals_.cache_misses += pmu_cache_misses().read_value();
    totals_.branches += pmu_branches().read_value();
    totals_.branch_misses += pmu_branch_misses().read_value();
    totals_.page_faults += pmu_page_faults().read_value();
    totals_.l1d_references += pmu_l1d_references().read_value();
    totals_.l1d_misses += pmu_l1d_misses().read_value();
    totals_.l1i_references += pmu_l1i_references().read_value();
    totals_.l1i_misses += pmu_l1i_misses().read_value();
    totals_.l2_references += pmu_l2_references().read_value();
    totals_.l2_misses += pmu_l2_misses().read_value();
    totals_.l3_references += pmu_l3_references().read_value();
    totals_.l3_misses += pmu_l3_misses().read_value();
}

static double pmu_ratio(uint64_t numerator, uint64_t denominator) {
    return denominator == 0
               ? 0.0
               : 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

void print_pmu_profile_totals(const PmuProfileTotals& totals, FILE* out) {
    fprintf(out,
            "[pmu] process_market_signal_and_order calls=%llu cycles=%llu "
            "instructions=%llu cache-references=%llu cache-misses=%llu "
            "cache-miss-rate=%.4f%% branches=%llu branch-misses=%llu "
            "branch-miss-rate=%.4f%% page-faults=%llu\n",
            static_cast<unsigned long long>(totals.calls),
            static_cast<unsigned long long>(totals.cycles),
            static_cast<unsigned long long>(totals.instructions),
            static_cast<unsigned long long>(totals.cache_references),
            static_cast<unsigned long long>(totals.cache_misses),
            pmu_ratio(totals.cache_misses, totals.cache_references),
            static_cast<unsigned long long>(totals.branches),
            static_cast<unsigned long long>(totals.branch_misses),
            pmu_ratio(totals.branch_misses, totals.branches),
            static_cast<unsigned long long>(totals.page_faults));
    fprintf(out,
            "[pmu] cache-levels L1D-references=%llu L1D-misses=%llu L1D-miss-rate=%.4f%% "
            "L1I-references=%llu L1I-misses=%llu L1I-miss-rate=%.4f%% "
            "L2-references=%llu L2-misses=%llu L2-miss-rate=%.4f%% "
            "L3-references=%llu L3-misses=%llu L3-miss-rate=%.4f%%\n",
            static_cast<unsigned long long>(totals.l1d_references),
            static_cast<unsigned long long>(totals.l1d_misses),
            pmu_ratio(totals.l1d_misses, totals.l1d_references),
            static_cast<unsigned long long>(totals.l1i_references),
            static_cast<unsigned long long>(totals.l1i_misses),
            pmu_ratio(totals.l1i_misses, totals.l1i_references),
            static_cast<unsigned long long>(totals.l2_references),
            static_cast<unsigned long long>(totals.l2_misses),
            pmu_ratio(totals.l2_misses, totals.l2_references),
            static_cast<unsigned long long>(totals.l3_references),
            static_cast<unsigned long long>(totals.l3_misses),
            pmu_ratio(totals.l3_misses, totals.l3_references));
}

}  // namespace nts::instrument
