python3 tools/bench_runner.py \
  --pipeline-a ./build/nts_pipeline_baseline \
  --pipeline-b ./build/nts_pipeline \
  --duration 10

python3 tools/bench_runner.py \
  --exchange-core 3 \
  --client-a-core 1 \
  --client-b-core 2