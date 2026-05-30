# cmake/core/core.cmake

message(STATUS "Configuring Core Layer")

add_library(v2_core STATIC)

target_sources(v2_core PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/actor/actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/ring_buffer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/time.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/mutex/mutex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/semaphore/semaphore.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/sleep/sleep.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/thread/thread.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/timer/timer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/dispatcher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/executor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/scheduler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runtime/worker.cpp
)

target_include_directories(v2_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
