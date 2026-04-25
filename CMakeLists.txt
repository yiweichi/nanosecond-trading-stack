cmake_minimum_required(VERSION 3.16)
project(nanosecond-trading-stack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(NTS_ENABLE_TRACING "Enable hop-by-hop latency tracing" ON)

add_library(nts_core STATIC
    src/instrument/tracer.cpp
    src/instrument/stats.cpp
    src/market_data.cpp
    src/orderbook.cpp
    src/strategy.cpp
    src/oms.cpp
    src/exchange.cpp
    src/order_gateway.cpp
    src/matching/orderbook.cpp
)

target_include_directories(nts_core PUBLIC include)

target_compile_options(nts_core PRIVATE -Wall -Wextra -Wpedantic)

if(NTS_ENABLE_TRACING)
    target_compile_definitions(nts_core PUBLIC NTS_ENABLE_TRACING)
endif()

# Main pipeline executable
add_executable(nts_pipeline src/main.cpp)
target_link_libraries(nts_pipeline PRIVATE nts_core)
target_compile_options(nts_pipeline PRIVATE -Wall -Wextra -Wpedantic)

# Market data generator tool
add_executable(md_generator tools/md_generator.cpp)
target_link_libraries(md_generator PRIVATE nts_core)
target_compile_options(md_generator PRIVATE -Wall -Wextra -Wpedantic)

# Self-contained pipeline benchmark
add_executable(pipeline_bench benchmarks/pipeline_bench.cpp)
target_link_libraries(pipeline_bench PRIVATE nts_core)
target_compile_options(pipeline_bench PRIVATE -Wall -Wextra -Wpedantic)

# Matching engine benchmark + tests
add_executable(matching_bench benchmarks/matching_bench.cpp)
target_link_libraries(matching_bench PRIVATE nts_core)
target_compile_options(matching_bench PRIVATE -Wall -Wextra -Wpedantic)
