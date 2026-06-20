# cmake/service/service.cmake

message(STATUS "Configuring Service Layer")

add_library(v2_service STATIC)

target_sources(v2_service PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/ipc/ipc_server_actor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tick/tick_actor.cpp
)

target_include_directories(v2_service PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)

target_compile_definitions(v2_service PUBLIC
    IPC_SERVER_ACTOR_DATA_RECV_BUFFER_SIZE=4096
)
