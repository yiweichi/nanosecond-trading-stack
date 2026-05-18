#pragma once

#include <cstdint>
#include <cstdio>

namespace nts::instrument {

enum class PerfEvent : uint8_t {
    CpuCycles       = 0,
    Instructions    = 1,
    CacheReferences = 2,
    CacheMisses     = 3,
    Branches        = 4,
    BranchMisses    = 5,
    PageFaults      = 6,
    L1DReferences   = 100,
    L1DMisses       = 101,
    L1IReferences   = 102,
    L1IMisses       = 103,
    L2References    = 104,
    L2Misses        = 105,
    L3References    = 106,
    L3Misses        = 107,
};

class PerfCounter {
public:
    PerfCounter() = default;
    explicit PerfCounter(PerfEvent event);
    ~PerfCounter();  // NOLINT(performance-trivially-destructible)

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

struct PmuProfileTotals {
    uint64_t calls            = 0;
    uint64_t cycles           = 0;
    uint64_t instructions     = 0;
    uint64_t cache_references = 0;
    uint64_t cache_misses     = 0;
    uint64_t branches         = 0;
    uint64_t branch_misses    = 0;
    uint64_t page_faults      = 0;
    uint64_t l1d_references   = 0;
    uint64_t l1d_misses       = 0;
    uint64_t l1i_references   = 0;
    uint64_t l1i_misses       = 0;
    uint64_t l2_references    = 0;
    uint64_t l2_misses        = 0;
    uint64_t l3_references    = 0;
    uint64_t l3_misses        = 0;
};

class PmuProfileScope {
public:
    explicit PmuProfileScope(PmuProfileTotals& totals);
    ~PmuProfileScope();

    PmuProfileScope(const PmuProfileScope&)            = delete;
    PmuProfileScope& operator=(const PmuProfileScope&) = delete;

private:
    PmuProfileTotals& totals_;
};

void print_pmu_profile_totals(const PmuProfileTotals& totals, FILE* out);

}  // namespace nts::instrument
