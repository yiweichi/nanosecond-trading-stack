#include "nts/perf_counter.h"

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace nts::instrument {

static long perf_event_open(perf_event_attr* hw_event, pid_t pid, int cpu, int group_fd,
                            unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
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
            attr.config = PERF_COUNT_HW_CACHE_L1D |
                          (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L1DMisses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1D |
                          (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
        case PerfEvent::L1IReferences:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1I |
                          (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L1IMisses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1I |
                          (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
        case PerfEvent::L2References:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL |
                          (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L2Misses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL |
                          (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
        case PerfEvent::L3References:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL |
                          (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
            break;
        case PerfEvent::L3Misses:
            attr.type   = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_LL |
                          (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
            break;
    }

    config_ = attr.config;

    fd_ = static_cast<int>(perf_event_open(&attr, 0, -1, -1, 0));
    if (fd_ == -1) {
        throw std::runtime_error(std::string("perf_event_open failed: ") + std::strerror(errno));
    }
}

void PerfCounter::reset() const {
    ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
}

void PerfCounter::enable() const {
    ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
}

void PerfCounter::disable() const {
    ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
}

uint64_t PerfCounter::read_value() const {
    uint64_t value = 0;
    const ssize_t n = ::read(fd_, &value, sizeof(value));
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

void PerfCounter::reset() const {}
void PerfCounter::enable() const {}
void PerfCounter::disable() const {}
uint64_t PerfCounter::read_value() const {
    return 0;
}

}  // namespace nts::instrument
#endif
