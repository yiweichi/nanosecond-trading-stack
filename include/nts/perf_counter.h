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
    L1DLoads        = 100,
    L1DLoadMisses   = 101,
    L1DStores       = 102,
    LLCLoads        = 110,
    LLCLoadMisses   = 111,
    LLCStores       = 112,
    LLCStoreMisses  = 113,
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
    uint64_t l1d_loads        = 0;
    uint64_t l1d_load_misses  = 0;
    uint64_t l1d_stores       = 0;
    uint64_t llc_loads        = 0;
    uint64_t llc_load_misses  = 0;
    uint64_t llc_stores       = 0;
    uint64_t llc_store_misses = 0;
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
