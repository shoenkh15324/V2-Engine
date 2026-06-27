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
    ${CMAKE_CURRENT_LIST_DIR}/common/timer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/runtime_config.cpp
)

target_compile_definitions(v2_core PUBLIC
    V2_ENGINE_NAME="${PROJECT_NAME}"
    V2_ENGINE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(v2_core PUBLIC
    nlohmann_json::nlohmann_json
    ftxui::ftxui
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
