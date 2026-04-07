# nanosecond-trading-stack

A minimal ultra-low-latency trading stack in C++ covering
market data → strategy → order → execution.

## What This Is

A single-process, single-threaded trading pipeline that receives UDP market data,
maintains a local order book, runs an imbalance strategy, manages orders through
an OMS, and executes against a mock exchange — all with nanosecond-granularity
latency instrumentation at every hop.

## Architecture

```
UDP Market Data → OrderBook → Strategy → OMS → Mock Exchange → Ack → OMS
```

See [PLAN.md](PLAN.md) for detailed architecture, design decisions, and
optimization roadmap.

## Prerequisites

- C++17 compiler (Clang or GCC)
- CMake ≥ 3.16
- Make (or Ninja)

## Quick Start

```bash
make release      # Release build (-O2)
make bench        # Pipeline benchmark
make match-bench  # Matching engine benchmark
```

See [USAGE.md](USAGE.md) for the full list of make targets, scenarios, and
recommended parameters.

## Latency Instrumentation

Every pipeline iteration records nanosecond timestamps at each hop:

```
RecvStart → RecvDone → BookUpdated → StrategyDone → OrderSent → AckReceived → AckProcessed
```

The report shows per-hop and end-to-end latency distributions
(P50 / P90 / P99 / P99.9 / Max) in nanoseconds.

Production-grade tracer design:
- All hot-path functions are header-inline (zero function-call overhead)
- Stores raw hardware ticks; converts to nanoseconds on the cold path
- Ring buffer — overwrites oldest traces instead of dropping new ones
- `alignas(64)` ensures each trace record fits one cache line
- Branch hints (`__builtin_expect`) for predictable hot-path branching

Tracing can be fully disabled at compile time with zero runtime overhead:

```bash
cmake .. -DNTS_ENABLE_TRACING=OFF
```

## Matching Engine (Rust)

The `matching-engine/` submodule is a standalone Rust order-matching engine
with its own benchmarks. Initialize and build it separately:

```bash
git submodule update --init --recursive
cd matching-engine
cargo run --release
```

## License

MIT
