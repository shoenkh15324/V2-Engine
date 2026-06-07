# cmake/core/core.cmake

message(STATUS "Configuring Core Layer")

add_library(v2_core STATIC)

target_sources(v2_core PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/actor/actor_registry.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/actor_system.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/actor/actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/actor/actor_context.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/dispatcher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/scheduler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/actor_system/runtime/worker.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/log.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/ring_buffer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/time.cpp
)

if(WIN32)
    #
elseif(UNIX)
    target_sources(v2_core PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/common/epoll.cpp
    )
endif()

target_include_directories(v2_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
