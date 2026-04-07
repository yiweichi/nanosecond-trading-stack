# ── Nanosecond Trading Stack — Build & Run ────────────────────────────────────
#
# Targets:
#   make debug        Build in Debug mode (-O0 -g, for lldb/gdb)
#   make release      Build in Release mode (-O2)
#   make clean        Remove the build directory
#   make bench        Release build + run pipeline benchmark (saves to results/)
#   make match-bench  Release build + run matching engine benchmark (saves to results/)
#   make match-scenario SCENARIO=<name> [DEPTH=n] [LEVELS=n] [ORDERS=n]
#   make match-profile  SCENARIO=<name> [DEPTH=n] [LEVELS=n] [ORDERS=n] [REPEAT=n]
#   make run          Release build + start the UDP trading pipeline
#   make gen          Release build + start the market data generator
#   make fmt          Format all C++ sources with clang-format
#   make fmt-check    Dry-run format check (CI-friendly, fails on diff)
#   make lint         Run clang-tidy on all sources
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
RATE        ?= 5000

SRCS := $(shell find src -name '*.cpp') $(shell find benchmarks -name '*.cpp')
HDRS := $(shell find include -name '*.h')
ALL_FILES := $(SRCS) $(HDRS)

.PHONY: debug release clean bench match-bench match-scenario match-profile run gen fmt fmt-check lint

release:
	@mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j

debug:
	@mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) && make -j

clean:
	@rm -rf $(BUILD_DIR)

bench: release
	@./$(BUILD_DIR)/pipeline_bench $(BENCH_ITERS) $(BENCH_WARM)

match-bench: release
	@./$(BUILD_DIR)/matching_bench

match-scenario: release
	@test -n "$(SCENARIO)" || (echo "usage: make match-scenario SCENARIO=<name> [DEPTH=n] [LEVELS=n] [ORDERS=n]"; exit 1)
	@./$(BUILD_DIR)/matching_bench bench --scenario $(SCENARIO) $(if $(DEPTH),--depth $(DEPTH),) $(if $(LEVELS),--levels $(LEVELS),) $(if $(ORDERS),--orders $(ORDERS),)

match-profile: release
	@test -n "$(SCENARIO)" || (echo "usage: make match-profile SCENARIO=<name> [DEPTH=n] [LEVELS=n] [ORDERS=n] [REPEAT=n]"; exit 1)
	@./$(BUILD_DIR)/matching_bench profile --scenario $(SCENARIO) $(if $(DEPTH),--depth $(DEPTH),) $(if $(LEVELS),--levels $(LEVELS),) $(if $(ORDERS),--orders $(ORDERS),) $(if $(REPEAT),--repeat $(REPEAT),)

run: release
	@./$(BUILD_DIR)/nts_pipeline $(PORT) $(DURATION)

gen: release
	@./$(BUILD_DIR)/md_generator $(PORT) $(RATE)

fmt:
	@clang-format -i $(ALL_FILES)
	@echo "Formatted $$(echo $(ALL_FILES) | wc -w | tr -d ' ') files."

fmt-check:
	@clang-format --dry-run -Werror $(ALL_FILES)

lint: release
	@clang-tidy $(SRCS) -- -Iinclude -std=c++17
