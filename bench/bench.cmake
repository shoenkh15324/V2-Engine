# cmake/bench/bench.cmake

message(STATUS "Configuring Bench Layer")

add_library(v2_bench OBJECT
    ${CMAKE_CURRENT_LIST_DIR}/bench_backpressure.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench_contention.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench_latency.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench_scaling.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench_scheduler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench_throughput.cpp
    ${CMAKE_CURRENT_LIST_DIR}/benchmark.cpp
)

target_link_libraries(v2_bench PUBLIC v2_core)

target_include_directories(v2_bench PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../src
    ${CMAKE_CURRENT_LIST_DIR}/..
)

option(BUILD_BENCH_APP "Build standalone benchmark app" ON)

if(BUILD_BENCH_APP)
    add_executable(v2_bench_cli
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    )
    target_include_directories(v2_bench_cli PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../src
        ${CMAKE_CURRENT_LIST_DIR}/..
    )
    target_link_libraries(v2_bench_cli PRIVATE v2_bench v2_core)
endif()