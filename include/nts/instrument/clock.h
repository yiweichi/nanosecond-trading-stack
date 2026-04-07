#pragma once

#include <cstdint>

#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

namespace nts::instrument {

// ── Platform-specific raw tick source ────────────────────────────────────────
// raw_ticks() is the fastest monotonic read on each platform.
// ticks_to_ns() converts a tick *delta* to nanoseconds (call on cold path).

#ifdef __APPLE__

inline uint64_t raw_ticks() {
    return mach_absolute_time();
}

inline uint64_t ticks_to_ns(uint64_t ticks) {
    static mach_timebase_info_data_t info = [] {
        mach_timebase_info_data_t i;
        mach_timebase_info(&i);
        return i;
    }();
    return ticks * info.numer / info.denom;
}

#else

inline uint64_t raw_ticks() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

inline uint64_t ticks_to_ns(uint64_t ticks) {
    return ticks;
}

#endif

inline uint64_t now_ns() {
    return ticks_to_ns(raw_ticks());
}

// ── Architecture-specific raw counters (reserved for future use) ─────────────

#if defined(__x86_64__) || defined(_M_X64)
inline uint64_t rdtsc() {
    uint32_t hi, lo;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#elif defined(__aarch64__) || defined(_M_ARM64)
inline uint64_t read_cntvct() {
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}
#endif

} // namespace nts::instrument
