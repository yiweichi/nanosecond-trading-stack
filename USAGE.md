# Usage Reference

## Build

| Command | Description |
|---|---|
| `make debug` | Debug build (`-O0 -g`, for lldb/gdb) |
| `make release` | Release build (`-O2`) |
| `make clean` | Remove the build directory |

## Pipeline

| Command | Description |
|---|---|
| `make run` | Start the UDP trading pipeline (10s default) |
| `make gen` | Start the market data generator |
| `make run PORT=9999 DURATION=30` | Custom port and duration |
| `make gen PORT=9999 RATE=5000` | Custom port and send rate |

The pipeline requires two terminals: `make gen` in one, `make run` in another.

## Pipeline Benchmark

| Command | Description |
|---|---|
| `make bench` | Run pipeline benchmark (100K iterations) |
| `make bench BENCH_ITERS=500000 BENCH_WARM=50000` | Custom iterations/warmup |

Results saved to `results/pipeline/mac/<timestamp>.txt`.

## Matching Engine Benchmark

| Command | Description |
|---|---|
| `make match-bench` | Run full benchmark suite (all 8 scenarios) |
| `make match-scenario SCENARIO=<name>` | Bench a single scenario |
| `make match-profile SCENARIO=<name>` | Profile mode (no timing, for perf/flamegraph) |

Results saved to `results/matching/mac/<timestamp>.txt` with per-scenario
histogram CSVs in `results/matching/mac/<timestamp>_histograms/`.

### Scenarios

| Scenario | Description | Key parameter |
|---|---|---|
| `passive-insert` | Insert limit orders that don't cross the spread | `DEPTH` |
| `aggressive-fill` | Insert a buy that fills against best ask | `DEPTH` |
| `multi-level-sweep` | Buy sweeps through N price levels | `LEVELS` |
| `market-order` | Submit a 1-lot market order | `DEPTH` |
| `cancel` | Cancel a resting order by ID | `DEPTH` |
| `cancel-hot-level` | Cancel from a single crowded price level | `ORDERS` |
| `drain-single-level` | One buy drains all resting asks at one price | `ORDERS` |
| `mixed-workload` | 65% cancel, 25% insert, 10% fill | `DEPTH` |

### Parameter Reference

| Parameter | Makefile variable | Default sweep | Recommended single values |
|---|---|---|---|
| Book depth | `DEPTH` | 0, 100, 10K, 100K | `100`, `10000`, `100000` |
| Price levels | `LEVELS` | 1, 5, 10, 50 | `5`, `10`, `50` |
| Orders | `ORDERS` | 10, 100, 1K, 10K | `100`, `1000`, `10000` |
| Repeat (profile only) | `REPEAT` | 1 | `3`–`10` for stable flamegraphs |

When a parameter is omitted, the benchmark sweeps across all default values.
When specified, only the given value is benchmarked.

### Examples

```bash
# Full suite
make match-bench

# Single scenario, default sweep
make match-scenario SCENARIO=cancel

# Single scenario, single depth
make match-scenario SCENARIO=passive-insert DEPTH=100000

# Sweep levels for multi-level-sweep
make match-scenario SCENARIO=multi-level-sweep LEVELS=50

# Profile for flamegraph (no timing overhead, repeat 5x)
make match-profile SCENARIO=aggressive-fill DEPTH=10000 REPEAT=5

# Profile cancel with a crowded level
make match-profile SCENARIO=cancel-hot-level ORDERS=10000 REPEAT=3
```

### Profile Mode

Profile mode runs the exact same workload as bench but without any timing
or histogram recording. This eliminates measurement overhead, making it ideal
for `perf record`, `Instruments.app`, or flamegraph generation:

```bash
# Linux
perf record -g -- ./build/matching_bench profile --scenario cancel --depth 10000 --repeat 10
perf script | stackcollapse-perf.pl | flamegraph.pl > cancel.svg

# macOS
make match-profile SCENARIO=cancel DEPTH=10000 REPEAT=10
# Then attach Instruments.app to the process, or use `sample`
```

## Code Quality

| Command | Description | Equivalent |
|---|---|---|
| `make fmt` | Format all `.cpp` and `.h` files | `cargo fmt` |
| `make fmt-check` | Dry-run format check (fails on diff) | `cargo fmt --check` |
| `make lint` | Static analysis with clang-tidy | `cargo clippy` |

Requires `clang-format` and `clang-tidy` (via `brew install clang-format llvm`).
