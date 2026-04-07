#pragma once

#include <cstdint>

#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

namespace nts::instrument {

#ifdef __APPLE__

inline uint64_t now_ns() {
    static mach_timebase_info_data_t info = [] {
        mach_timebase_info_data_t i;
        mach_timebase_info(&i);
        return i;
    }();
    return mach_absolute_time() * info.numer / info.denom;
}

#else

inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

#endif

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
