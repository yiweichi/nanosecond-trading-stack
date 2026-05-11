#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef __linux__
#error "tsc_sync_check.cpp is Linux-only; run it on the Ubuntu host."
#endif

#if !defined(__x86_64__) && !defined(__i386__)
#error "tsc_sync_check.cpp requires x86/x86_64 rdtscp support."
#endif

namespace {

struct Config {
    int reference_core = 0;
    std::vector<int> cores;
    int samples = 100000;
    int warmup = 1000;
    int threshold_cycles = 1000;
};

struct Sample {
    int64_t offset_cycles = 0;
    uint64_t roundtrip_cycles = 0;
};

struct PairStats {
    int core = 0;
    int64_t best_offset_cycles = 0;
    uint64_t best_roundtrip_cycles = 0;
    uint64_t uncertainty_cycles = 0;
    int64_t low_bound_cycles = 0;
    int64_t high_bound_cycles = 0;
    uint32_t reference_aux = 0;
    uint32_t target_aux = 0;
    bool pass = false;
};

struct Shared {
    std::atomic<uint64_t> request{0};
    std::atomic<uint64_t> ack{0};
    std::atomic<bool> ready{false};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> target_tsc{0};
    std::atomic<uint32_t> target_aux{0};
};

inline uint64_t rdtscp(uint32_t* aux) {
    uint32_t lo = 0;
    uint32_t hi = 0;
    uint32_t cpu = 0;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(cpu) : : "memory");
    asm volatile("lfence" ::: "memory");
    if (aux != nullptr) {
        *aux = cpu;
    }
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

int64_t tsc_delta(uint64_t lhs, uint64_t rhs) {
    if (lhs >= rhs) {
        return static_cast<int64_t>(lhs - rhs);
    }
    return -static_cast<int64_t>(rhs - lhs);
}

void pin_current_thread(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        std::ostringstream oss;
        oss << "pthread_setaffinity_np(core=" << core << ") failed: " << std::strerror(rc);
        throw std::runtime_error(oss.str());
    }
}

int online_cores() {
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 0 || n > std::numeric_limits<int>::max()) {
        throw std::runtime_error("failed to determine online CPU count");
    }
    return static_cast<int>(n);
}

std::vector<int> parse_cores(const std::string& value) {
    const int n = online_cores();
    if (value == "all") {
        std::vector<int> cores;
        cores.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            cores.push_back(i);
        }
        return cores;
    }

    std::vector<int> cores;
    std::stringstream ss(value);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) {
            continue;
        }
        char* end = nullptr;
        errno = 0;
        const long core = std::strtol(token.c_str(), &end, 10);
        if (errno != 0 || end == token.c_str() || *end != '\0' || core < 0 || core >= n) {
            throw std::runtime_error("invalid core in --cores: " + token);
        }
        cores.push_back(static_cast<int>(core));
    }
    return cores;
}

void usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0 << " [--reference-core N] [--cores all|A,B,C]\n"
        << "       [--samples N] [--warmup N] [--threshold-cycles N]\n\n"
        << "Example:\n"
        << "  g++ -O2 -std=c++17 -pthread tools/tsc_sync_check.cpp -o /tmp/tsc_sync_check\n"
        << "  sudo /tmp/tsc_sync_check --reference-core 0 --cores all --samples 200000\n";
}

Config parse_args(int argc, char** argv) {
    Config cfg;
    cfg.cores = parse_cores("all");

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--reference-core") {
            cfg.reference_core = std::atoi(require_value("--reference-core"));
        } else if (arg == "--cores") {
            cfg.cores = parse_cores(require_value("--cores"));
        } else if (arg == "--samples") {
            cfg.samples = std::atoi(require_value("--samples"));
        } else if (arg == "--warmup") {
            cfg.warmup = std::atoi(require_value("--warmup"));
        } else if (arg == "--threshold-cycles") {
            cfg.threshold_cycles = std::atoi(require_value("--threshold-cycles"));
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    const int n = online_cores();
    if (cfg.reference_core < 0 || cfg.reference_core >= n) {
        throw std::runtime_error("invalid --reference-core");
    }
    if (cfg.samples <= 0 || cfg.warmup < 0 || cfg.threshold_cycles < 0) {
        throw std::runtime_error("--samples must be positive; --warmup and --threshold-cycles non-negative");
    }
    return cfg;
}

void target_loop(Shared* shared, int target_core) {
    pin_current_thread(target_core);
    shared->ready.store(true, std::memory_order_release);

    uint64_t seen = 0;
    while (!shared->stop.load(std::memory_order_acquire)) {
        uint64_t seq = shared->request.load(std::memory_order_acquire);
        if (seq == seen) {
            continue;
        }
        seen = seq;

        uint32_t aux = 0;
        const uint64_t tsc = rdtscp(&aux);
        shared->target_tsc.store(tsc, std::memory_order_relaxed);
        shared->target_aux.store(aux, std::memory_order_relaxed);
        shared->ack.store(seq, std::memory_order_release);
    }
}

PairStats measure_pair(const Config& cfg, int target_core) {
    Shared shared;
    std::thread target([&] { target_loop(&shared, target_core); });

    while (!shared.ready.load(std::memory_order_acquire)) {
    }

    pin_current_thread(cfg.reference_core);

    const int total_samples = cfg.warmup + cfg.samples;
    Sample best;
    best.roundtrip_cycles = std::numeric_limits<uint64_t>::max();
    uint32_t best_ref_aux = 0;
    uint32_t best_target_aux = 0;

    for (int i = 0; i < total_samples; ++i) {
        const uint64_t seq = static_cast<uint64_t>(i + 1);
        uint32_t ref_aux = 0;
        const uint64_t start = rdtscp(&ref_aux);
        shared.request.store(seq, std::memory_order_release);
        while (shared.ack.load(std::memory_order_acquire) != seq) {
        }
        const uint64_t end = rdtscp(nullptr);

        if (i < cfg.warmup) {
            continue;
        }

        const uint64_t target_tsc = shared.target_tsc.load(std::memory_order_relaxed);
        const uint64_t roundtrip = end - start;
        const uint64_t midpoint = start + roundtrip / 2;
        const int64_t offset = tsc_delta(target_tsc, midpoint);

        if (roundtrip < best.roundtrip_cycles) {
            best.roundtrip_cycles = roundtrip;
            best.offset_cycles = offset;
            best_ref_aux = ref_aux;
            best_target_aux = shared.target_aux.load(std::memory_order_relaxed);
        }
    }

    shared.stop.store(true, std::memory_order_release);
    shared.request.fetch_add(1, std::memory_order_release);
    target.join();

    PairStats stats;
    stats.core = target_core;
    stats.best_offset_cycles = best.offset_cycles;
    stats.best_roundtrip_cycles = best.roundtrip_cycles;
    stats.uncertainty_cycles = (best.roundtrip_cycles + 1) / 2;
    stats.low_bound_cycles = stats.best_offset_cycles - static_cast<int64_t>(stats.uncertainty_cycles);
    stats.high_bound_cycles = stats.best_offset_cycles + static_cast<int64_t>(stats.uncertainty_cycles);
    stats.reference_aux = best_ref_aux;
    stats.target_aux = best_target_aux;
    stats.pass = std::llabs(stats.best_offset_cycles) <=
                 static_cast<long long>(cfg.threshold_cycles + stats.uncertainty_cycles);
    return stats;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Config cfg = parse_args(argc, argv);

        std::cout << "reference_core=" << cfg.reference_core << " samples=" << cfg.samples
                  << " warmup=" << cfg.warmup
                  << " threshold_cycles=" << cfg.threshold_cycles << "\n\n";
        std::cout << "core  best_offset  uncertainty  bound_low  bound_high  roundtrip  ref_aux  target_aux  verdict\n";
        std::cout << "----  -----------  -----------  ---------  ----------  ---------  -------  ----------  -------\n";

        bool all_pass = true;
        for (int core : cfg.cores) {
            if (core == cfg.reference_core) {
                continue;
            }
            const PairStats stats = measure_pair(cfg, core);
            all_pass = all_pass && stats.pass;
            std::cout << core << "  " << stats.best_offset_cycles << "  "
                      << stats.uncertainty_cycles << "  " << stats.low_bound_cycles << "  "
                      << stats.high_bound_cycles << "  " << stats.best_roundtrip_cycles << "  "
                      << stats.reference_aux << "  " << stats.target_aux << "  "
                      << (stats.pass ? "ok" : "check") << "\n";
        }

        std::cout << "\nInterpretation: best_offset is target_core_tsc - reference_core_tsc at the\n"
                  << "midpoint of the lowest-latency ping-pong sample. If zero is inside the\n"
                  << "[bound_low, bound_high] interval, the measurement did not prove skew.\n";
        return all_pass ? 0 : 2;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        usage(argv[0]);
        return 1;
    }
}
