# cmake/infra/infra.cmake

message(STATUS "Configuring Infra Layer")

add_library(v2_infra STATIC)

target_sources(v2_infra PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/hal/dummy.cpp
)

target_include_directories(v2_infra PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)

# Compile options
if(WIN32)
    target_compile_options(v2_infra PRIVATE /W4)
elseif(UNIX)
    target_compile_options(v2_infra PRIVATE -Wall -Wextra -Wpedantic)
endif()
