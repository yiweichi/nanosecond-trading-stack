#pragma once

#include <cstdint>

namespace nts::instrument {

enum class PerfEvent : uint64_t {
    CpuCycles    = 0,
    Instructions = 1,
    CacheMisses  = 3,
    BranchMisses = 5,
};

class PerfCounter {
public:
    PerfCounter() = default;
    explicit PerfCounter(PerfEvent event);
    ~PerfCounter();

    PerfCounter(const PerfCounter&)            = delete;
    PerfCounter& operator=(const PerfCounter&) = delete;

    void     init(PerfEvent event);
    void     reset() const;
    void     enable() const;
    void     disable() const;
    uint64_t read_value() const;

private:
    [[maybe_unused]] int      fd_     = -1;
    [[maybe_unused]] uint64_t config_ = 0;
};

}  // namespace nts::instrument
