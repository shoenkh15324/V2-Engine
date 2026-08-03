cmake_minimum_required(VERSION 3.14.0)
project(V2_CORE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(v2_core OBJECT
    actor_system/actor_system.cpp
    actor_system/actor/actor.cpp
    actor_system/actor/actor_handle.cpp
    actor_system/actor/actor_registry.cpp
    actor_system/runtime/actor_runtime/actor_runtime.cpp
    actor_system/runtime/dispatcher/work_dispatcher.cpp
    actor_system/runtime/scheduler/scheduler.cpp
    actor_system/runtime/supervisor/supervisor.cpp
    actor_system/runtime/dispatcher/worker.cpp
    common/log/log.cpp
    common/container/ring_buffer.cpp
    common/time/time.cpp
    common/timer/timer_base.cpp
    common/timer/timer.cpp
    perf/metrics/metrics.cpp
)

target_include_directories(v2_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../)

find_package(Threads REQUIRED)
target_link_libraries(v2_core PUBLIC Threads::Threads)
