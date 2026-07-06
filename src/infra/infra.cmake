# cmake/infra/infra.cmake

message(STATUS "Configuring Infra Layer")

add_library(v2_infra STATIC)

target_sources(v2_infra PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/hal/dummy.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(v2_infra PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/hal/i2c/i2c_linux.cpp
        ${CMAKE_CURRENT_LIST_DIR}/hal/pmu/pmu_rsp5.cpp
        ${CMAKE_CURRENT_LIST_DIR}/hal/sys/sys_linux.cpp
        ${CMAKE_CURRENT_LIST_DIR}/transport/uds/uds_server.cpp
        ${CMAKE_CURRENT_LIST_DIR}/transport/uds/uds_client.cpp
    )
endif()

target_link_libraries(v2_infra PUBLIC nlohmann_json::nlohmann_json)

target_include_directories(v2_infra PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)
