# cmake/core/core.cmake

message(STATUS "Configuring Core Layer")

add_library(v2_core STATIC)

target_sources(v2_core PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/actor/actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/actor_context.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/dispatcher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/scheduler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/worker.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/log.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/ring_buffer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/time.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/mutex/mutex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/semaphore/semaphore.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/sleep/sleep.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/thread/thread.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/timer/timer.cpp
)

target_include_directories(v2_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
