python3 tools/bench_runner.py \
  --pipeline-a ./build/nts_pipeline_baseline \
  --pipeline-b ./build/nts_pipeline \
  --duration 10

python3 tools/bench_runner.py \
  --exchange-core 3 \
  --client-a-core 1 \
  --client-b-core 2

## TSC sync check on Ubuntu

```bash
g++ -O2 -std=c++17 -pthread tools/tsc_sync_check.cpp -o /tmp/tsc_sync_check
sudo /tmp/tsc_sync_check --reference-core 0 --cores all --samples 200000
```

The tool reports the best observed TSC offset for each target core relative to
the reference core. Treat the lowest `roundtrip` rows as the most reliable; if
zero is inside `[bound_low, bound_high]`, the run did not prove cross-core TSC
skew.