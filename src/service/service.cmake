# cmake/service/service.cmake

message(STATUS "Configuring Service Layer")

add_library(v2_service STATIC)

target_sources(v2_service PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/dummy2.cpp
)

target_include_directories(v2_service PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)

# Compile options
if(WIN32)
    target_compile_options(v2_service PRIVATE /W4)
elseif(UNIX)
    target_compile_options(v2_service PRIVATE -Wall -Wextra -Wpedantic)
endif()
