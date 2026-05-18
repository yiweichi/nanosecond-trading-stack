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
    config_ = static_cast<uint64_t>(event);

    perf_event_attr attr{};
    attr.type           = PERF_TYPE_HARDWARE;
    attr.size           = sizeof(perf_event_attr);
    attr.config         = config_;
    attr.disabled       = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv     = 1;
    attr.inherit        = 0;

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
