# cmake/core/core.cmake

message(STATUS "Configuring Core Layer")

add_library(v2_core STATIC)

target_sources(v2_core PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/common/ring_buffer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/time.cpp
)

target_include_directories(v2_core PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)

# Compile options
if(WIN32)
    target_compile_options(v2_core PRIVATE /W4)
elseif(UNIX)
    target_compile_options(v2_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
