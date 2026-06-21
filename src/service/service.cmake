# cmake/service/service.cmake

message(STATUS "Configuring Service Layer")

add_library(v2_service STATIC
    ${CMAKE_CURRENT_LIST_DIR}/ipc/ipc_server_actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tick/tick_actor.cpp
)

target_link_libraries(v2_service PUBLIC v2_core)

target_include_directories(v2_service PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
