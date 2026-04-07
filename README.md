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

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j
```

## Run

```bash
# Self-contained benchmark (no network, synthetic data)
./pipeline_bench 100000 10000

# Full pipeline with UDP market data
./md_generator 12345 1000 &    # terminal 1: send fake market data
./nts_pipeline 12345 10        # terminal 2: run pipeline for 10 seconds
```

## Latency Instrumentation

Every pipeline iteration records nanosecond timestamps at each hop:

- `RecvStart` → `RecvDone` → `BookUpdated` → `StrategyDone` → `OrderSent`
- `AckReceived` → `AckProcessed`

The report shows P50/P90/P99/P99.9/Max for each segment and end-to-end.

Tracing can be fully disabled at compile time (`-DNTS_ENABLE_TRACING=OFF`)
with zero runtime overhead.

## License

MIT
