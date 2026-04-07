# ── Nanosecond Trading Stack — Build & Run ────────────────────────────────────
#
# Targets:
#   make debug        Build in Debug mode (-O0 -g, for lldb/gdb)
#   make release      Build in Release mode (-O2)
#   make clean        Remove the build directory
#   make bench        Release build + run in-process pipeline benchmark
#   make run          Release build + start the UDP trading pipeline
#   make gen          Release build + start the market data generator
#
# The pipeline requires two terminals:
#   Terminal 1:  make gen
#   Terminal 2:  make run
#
# Override defaults with environment variables:
#   make bench BENCH_ITERS=500000 BENCH_WARM=50000
#   make run   PORT=9999 DURATION=30
#   make gen   PORT=9999 RATE=5000
# ──────────────────────────────────────────────────────────────────────────────

BUILD_DIR   := build
BUILD_TYPE  ?= Debug
BENCH_ITERS ?= 100000
BENCH_WARM  ?= 10000
PORT        ?= 12345
DURATION    ?= 10
RATE        ?= 1000

.PHONY: debug release clean bench run gen

release:
	@mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j

debug:
	@mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) && make -j

clean:
	@rm -rf $(BUILD_DIR)

bench: release
	@./$(BUILD_DIR)/pipeline_bench $(BENCH_ITERS) $(BENCH_WARM)

run: release
	@./$(BUILD_DIR)/nts_pipeline $(PORT) $(DURATION)

gen: release
	@./$(BUILD_DIR)/md_generator $(PORT) $(RATE)
