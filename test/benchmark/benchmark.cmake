function(add_v2_benchmark name source)
    add_executable(${name} ${CMAKE_CURRENT_LIST_DIR}/${source})
    target_link_libraries(${name} PRIVATE v2_core benchmark::benchmark_main)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
endfunction()

add_v2_benchmark(benchmark_ring_buffer core/ring_buffer_bench.cpp)
add_v2_benchmark(benchmark_mailbox core/mailbox_bench.cpp)
add_v2_benchmark(benchmark_mailbox_multithread core/mailbox_multithread_bench.cpp)
add_v2_benchmark(benchmark_timer core/timer_bench.cpp)

# 벤치마크 전체 실행기
add_executable(benchmark_runner ${CMAKE_CURRENT_LIST_DIR}/core/bench_all_runner.cpp)
target_include_directories(benchmark_runner PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(benchmark_runner PRIVATE -Wl,--whole-archive v2_core -Wl,--no-whole-archive v2_service v2_infra)
else()
    target_link_libraries(benchmark_runner PRIVATE v2_core v2_service v2_infra)
endif()
