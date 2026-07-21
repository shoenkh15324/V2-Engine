# cmake/core/core.cmake

message(STATUS "Configuring Core Layer")

add_library(v2_core OBJECT)

target_sources(v2_core PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/actor_registry.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/actor_system.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/actor/actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/actor/actor_handle.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/actor_runtime.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/dispatcher/work_dispatcher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/dispatcher/io/event_loop_epoll.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/scheduler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/dispatcher/worker.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/log/log.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/container/ring_buffer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/time/time.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/time/timer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/config/runtime_config.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/os/signal_handler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/metrics/metrics.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/benchmark.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/bench_backpressure.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/bench_contention.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/bench_latency.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/bench_scaling.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/bench_scheduler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/bench_throughput.cpp 
)

target_compile_definitions(v2_core PUBLIC
    V2_ENGINE_NAME="${PROJECT_NAME}"
    V2_ENGINE_VERSION="${PROJECT_VERSION}"
    V2_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config"
)

target_link_libraries(v2_core PUBLIC
    nlohmann_json::nlohmann_json
    ftxui::ftxui
)

if(UNIX)
    target_sources(v2_core PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/common/os/epoll.cpp
    )
endif()

target_include_directories(v2_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
