# cmake/infra/infra.cmake

message(STATUS "Configuring Infra Layer")

add_library(v2_infra STATIC)

target_sources(v2_infra PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/transport/uds/uds_server.cpp
    ${CMAKE_CURRENT_LIST_DIR}/transport/uds/uds_client.cpp
)

target_include_directories(v2_infra PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
