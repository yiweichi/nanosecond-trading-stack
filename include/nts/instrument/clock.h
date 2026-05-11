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

#elif defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))

// On Linux x86_64 we use the invariant TSC directly.
//
// Memory ordering:
//   * `rdtsc` is *not* a serializing instruction — out-of-order execution can
//     reorder loads/stores around it. We bracket it with `lfence` so prior
//     instructions are retired before the timestamp is taken and subsequent
//     instructions cannot start before `lfence` completes.
//   * `rdtscp` waits for *prior* instructions to retire before sampling the
//     TSC, but does not block *later* instructions from executing ahead of
//     it — we still need a trailing `lfence`.
//
// `raw_ticks()` defaults to the `lfence; rdtsc; lfence` form so the same call
// is safe at both ends of a measured region. `raw_ticks_rdtscp()` exposes the
// `rdtscp; lfence` variant for end-of-region sampling when you want to
// guarantee everything in the region has retired.

inline uint64_t raw_ticks() {
    uint32_t lo, hi;
    __asm__ __volatile__(
        "lfence\n\t"
        "rdtsc\n\t"
        "lfence"
        : "=a"(lo), "=d"(hi)
        :
        : "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

inline uint64_t raw_ticks_rdtscp() {
    uint32_t lo, hi, aux;
    __asm__ __volatile__(
        "rdtscp\n\t"
        "lfence"
        : "=a"(lo), "=d"(hi), "=c"(aux)
        :
        : "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

namespace detail {

// Calibrate TSC frequency once per process against CLOCK_MONOTONIC.
// Returned value is nanoseconds-per-tick (a small fraction, e.g. ~0.4 on a
// 2.5 GHz core). Multiplying tick deltas by this value yields nanoseconds.
inline double calibrate_ns_per_tick() {
    struct timespec ts0;
    struct timespec ts1;

    clock_gettime(CLOCK_MONOTONIC, &ts0);
    uint64_t t0 = raw_ticks();

    // ~50 ms calibration window — long enough to dwarf clock_gettime jitter,
    // short enough not to delay process startup noticeably.
    struct timespec sleep_req {
        0, 50'000'000L
    };
    nanosleep(&sleep_req, nullptr);

    uint64_t t1 = raw_ticks();
    clock_gettime(CLOCK_MONOTONIC, &ts1);

    uint64_t ns =
        static_cast<uint64_t>(ts1.tv_sec - ts0.tv_sec) * 1'000'000'000ULL +
        static_cast<uint64_t>(ts1.tv_nsec - ts0.tv_nsec);
    uint64_t ticks = t1 - t0;
    if (ticks == 0) return 1.0;  // pathological — fall back to "1 tick = 1 ns"
    return static_cast<double>(ns) / static_cast<double>(ticks);
}

inline double ns_per_tick() {
    static const double v = calibrate_ns_per_tick();
    return v;
}

}  // namespace detail

inline uint64_t ticks_to_ns(uint64_t ticks) {
    return static_cast<uint64_t>(static_cast<double>(ticks) * detail::ns_per_tick());
}

#else

inline uint64_t raw_ticks() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
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
    uint64_t val = 0;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}
#endif

}  // namespace nts::instrument
