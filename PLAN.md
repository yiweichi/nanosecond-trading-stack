# nanosecond-trading-stack — Project Plan

## Overview

A minimal ultra-low-latency trading stack in C++ covering the full
**Market Data → Strategy → Order → Execution → Ack** pipeline.

The goal is not feature richness — it is **latency stability, measurability, and
explainability**. Every nanosecond of the critical path should be accounted for.

## Architecture

```
[matching-engine] ──UDP multicast──> [kernel] ──> [UDP sockets]
                                                          │
                                                    [recv buffer]
                                                          │
                                                    ┌─────▼──────┐
                                                    │  MD Parser  │  ← RecvDone
                                                    └─────┬──────┘
                                                          │
                                                    ┌─────▼──────┐
                                                    │  OrderBook  │  ← BookUpdated
                                                    └─────┬──────┘
                                                          │
                                                    ┌─────▼──────┐
                                                    │  Strategy   │  ← StrategyDone
                                                    └─────┬──────┘
                                                          │
                                                    ┌─────▼──────┐
                                                    │    OMS      │  ← OrderSent
                                                    └─────┬──────┘
                                                          │
                                                    ┌─────▼──────┐
                                                    │   Mock      │
                                                    │  Exchange   │  ← AckReceived
                                                    └─────┬──────┘
                                                          │
                                                    ┌─────▼──────┐
                                                    │  OMS (ack)  │  ← AckProcessed
                                                    └─────────────┘
```

**Threading model:** Single-threaded event loop. No locks, no contention.

**Mock Exchange** is an in-process module (not an external service). It simulates
exchange latency via timestamp comparison, never via `sleep`.

## Modules

| Module         | Header              | Responsibility                              |
|----------------|---------------------|---------------------------------------------|
| Common         | `common.h`          | Side enum, timestamp types, constants       |
| Clock          | `instrument/clock.h`| `now_ns()`, platform-specific cycle counter |
| Tracer         | `instrument/tracer.h`| Hop-by-hop timestamp recording             |
| Stats          | `instrument/stats.h`| Percentile computation, latency report      |
| Market Data    | `market_data.h`     | `MdMsg` struct, UDP receiver                |
| OrderBook      | `orderbook.h`       | L1 top-of-book state                        |
| Strategy       | `strategy.h`        | Imbalance-based signal generation           |
| OMS            | `oms.h`             | Order lifecycle, position tracking          |
| Mock Exchange  | `exchange.h`        | Simulated fill with configurable latency    |

## Instrumentation Design (Timestamp Every Hop)

This is the most critical subsystem. It must be:

1. **Modular** — decoupled from business logic via clean interface
2. **Pluggable** — enable/disable at compile time with zero overhead
3. **Accurate** — uses `CLOCK_MONOTONIC` (or rdtsc/cntvct on supported arch)
4. **Comprehensive** — captures every stage of the pipeline
5. **Survivable** — interface is stable across refactors

### Hop Points

```
RecvStart ──> RecvDone ──> BookUpdated ──> StrategyDone ──> OrderSent
                                                              │
                                              AckReceived <───┘
                                                  │
                                              AckProcessed
```

### Compile-time Toggle

```cpp
#ifdef NTS_ENABLE_TRACING
using ActiveTracer = HopTracer;   // full recording
#else
using ActiveTracer = NoopTracer;  // all calls compile to nothing
#endif
```

### Report Output

```
Per-Hop Latency (nanoseconds):
  Segment                        Samples      P50      P90      P99    P99.9      Max     Mean
  RecvStart -> RecvDone           100000      120      250      890     4200    15000      180
  RecvDone -> BookUpdated          99523       45       60       95      180      500       52
  ...

End-to-End:
  RecvDone -> StrategyDone         99523       75      102      173      330      900       87
  RecvStart -> OrderSent           12847      250      432     1193     4780    16500      329
```

## Implementation Phases

### Phase 1 — Infrastructure + UDP (current)
- [x] Directory structure, CMake, README, .gitignore
- [x] `common.h` — types and constants
- [x] `instrument/clock.h` — high-resolution timestamp
- [x] `instrument/tracer.h/.cpp` — hop tracer with enable/disable
- [x] `instrument/stats.h/.cpp` — percentile computation + report
- [x] `market_data.h/.cpp` — UDP receiver (non-blocking)
- [x] Rust matching engine — reference + quote multicast sender

### Phase 2 — Core Pipeline
- [x] `orderbook.h/.cpp` — L1 order book
- [x] `strategy.h/.cpp` — imbalance strategy
- [x] `oms.h/.cpp` — order management + position
- [x] `exchange.h/.cpp` — mock exchange with simulated latency
- [x] `main.cpp` — single-thread pipeline loop
- [x] `benchmarks/pipeline_bench.cpp` — self-contained benchmark

### Phase 3 — Baseline Measurement
- [ ] Run pipeline_bench, capture baseline P50/P99
- [ ] Run full pipeline (matching-engine + nts_pipeline)
- [ ] Record baseline numbers in BENCHMARKS.md

### Phase 4 — Optimization (one at a time, measure each)
- [ ] `alignas(64)` on hot structs (OrderBook, MdMsg)
- [ ] Eliminate remaining dynamic allocation on hot path
- [ ] Cache line padding to prevent false sharing
- [ ] `__builtin_expect` on predictable branches
- [ ] Struct field reordering for access pattern
- [ ] `SO_RCVBUF` tuning on UDP socket
- [ ] Busy-poll vs blocking recv comparison
- [ ] Compiler flags: `-O2` vs `-O3` vs `-march=native`

### Phase 5 — Advanced (optional)
- [ ] Multi-level order book (L2, 5 levels)
- [ ] Lock-free ring buffer between components
- [ ] SIMD for batch processing
- [ ] Kernel bypass exploration (raw sockets)
- [ ] Connect to real market data (Binance websocket)

## Key Design Decisions

| Decision                     | Rationale                                          |
|------------------------------|----------------------------------------------------|
| Single thread                | No lock contention, deterministic latency          |
| No `std::map` / `std::unordered_map` | Cache miss from pointer chasing           |
| No virtual functions on hot path | Avoid vtable indirection                      |
| No dynamic allocation on hot path | Avoid allocator latency spikes              |
| Fixed-size arrays            | Predictable memory layout                          |
| `recvfrom` non-blocking      | No blocking syscall stalling the pipeline          |
| Mock exchange in-process     | Removes network variable from measurement          |
| `clock_gettime(CLOCK_MONOTONIC)` | Portable, monotonic, nanosecond resolution     |

## Build & Run

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug    # unoptimized baseline
make -j

# Terminal 1: start matching engine
./matching-engine serve --md-port 12345 --order-port 12346

# Terminal 2: run pipeline
./nts_pipeline --port 12345 --order-port 12346 --duration 10

# Or run self-contained benchmark (no UDP needed)
./pipeline_bench 100000 10000
```
