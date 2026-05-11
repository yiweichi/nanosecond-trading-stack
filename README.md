# nanosecond-trading-stack

A compact low-latency trading lab for measuring market-data-to-order code paths
at nanosecond scale.

The stack has two main pieces:

- A C++17 single-threaded trading pipeline: UDP market data → local book →
  strategy → OMS → order gateway → execution reports.
- A Rust matching engine and exchange simulator: order book benchmarks, UDP
  market-data broadcast, TCP order gateway, stale-quote simulation, and
  two-client speed experiments.

## Architecture

```text
Rust Exchange
  ├─ UDP reference/target market data
  └─ TCP order gateway + execution reports

C++ Pipeline
  UDP MD → OrderBook → Strategy → OMS → OrderGateway → Rust Exchange
```

The live benchmark runner can start one exchange and two pipeline binaries so
small client-side optimizations can be compared against the same simulated
market. For benchmark-only experiments, the exchange can prioritize same-event
orders by client-reported reaction latency rather than TCP arrival jitter.

See [`PLAN.md`](PLAN.md) for design notes and [`USAGE.md`](USAGE.md) for the
full command reference.

## Prerequisites

- C++17 compiler (Clang or GCC)
- CMake ≥ 3.16
- Make or Ninja
- Rust toolchain for `matching-engine/`
- Linux for CPU pinning and hardware-timestamp tools; macOS works for most
  builds and local benchmarks, but without reliable process-to-core pinning

## Quick Start

```bash
make release      # Build C++ binaries
make bench        # Run C++ pipeline benchmark
make match-bench  # Run Rust matching-engine benchmark wrapper
```

Run the Rust exchange and two live clients through the helper:

```bash
python3 tools/bench_runner.py \
  --pipeline-a ./build/nts_pipeline_baseline \
  --pipeline-b ./build/nts_pipeline \
  --duration 10
```

The runner builds `matching-engine/target/release/matching-engine`, starts the
exchange, launches both clients, and summarizes latency, fills, stale captures,
and PnL. By default it uses `--order-priority client-reaction`; pass
`--order-priority random` to use the exchange's random tie-break behavior.

## Latency Instrumentation

Pipeline traces record raw hardware ticks at each hop and convert to
nanoseconds on the cold reporting path:

```text
RecvStart → RecvDone → BookUpdated → StrategyDone → OrderSent
OrderSent → AckReceived → AckProcessed
AckReceived → FillReceived → FillProcessed
```

Reports include per-hop and end-to-end latency distributions
(P50 / P90 / P99 / P99.9 / Max). On Linux x86_64 the clock path uses serialized
TSC reads with runtime calibration, so it does not assume that TSC ticks equal
the current turbo frequency.

Tracing can be compiled out:

```bash
cmake .. -DNTS_ENABLE_TRACING=OFF
```

## Matching Engine

`matching-engine/` contains the Rust order book, benchmark scenarios, and live
exchange server:

```bash
cargo build --manifest-path matching-engine/Cargo.toml --release
./matching-engine/target/release/matching-engine serve --help
```

Useful benchmark/profile scenarios include passive insert, aggressive fill,
cancel hot level, mixed workload, and side-prediction probes. See
[`matching-engine/docs/profile.md`](matching-engine/docs/profile.md) and
[`USAGE.md`](USAGE.md).

## Tools

- [`tools/bench_runner.py`](tools/bench_runner.py): two-client live speed
  comparison against one Rust exchange.
- [`tools/tsc_sync_check.cpp`](tools/tsc_sync_check.cpp): Ubuntu/x86 helper for
  checking whether TSC reads are synchronized across CPU cores.

TSC check example:

```bash
g++ -O2 -std=c++17 -pthread tools/tsc_sync_check.cpp -o /tmp/tsc_sync_check
sudo /tmp/tsc_sync_check --reference-core 0 --cores all --samples 200000
```

More tool examples are in [`tools/USAGE.md`](tools/USAGE.md).

## License

MIT
