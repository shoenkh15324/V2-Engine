# cmake/runtime/runtime.cmake

message(STATUS "Configuring Runtime Layer")

add_library(v2_runtime STATIC)

target_sources(v2_runtime PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/executor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/scheduler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/worker.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dispatcher.cpp
    
)

target_include_directories(v2_runtime PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)

# Compile options
if(WIN32)
    target_compile_options(v2_runtime PRIVATE /W4)
elseif(UNIX)
    target_compile_options(v2_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()
