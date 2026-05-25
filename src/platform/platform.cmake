# cmake/platform/platform.cmake

message(STATUS "Configuring Platform Layer")

add_library(v2_platform STATIC)

target_sources(v2_platform PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/osal/thread/thread.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/mutex/mutex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/semaphore/semaphore.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/timer/timer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/osal/sleep/sleep.cpp
)

target_include_directories(v2_platform PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../src
)

if(WIN32)
    target_sources(v2_platform PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/osal/mutex/mutex_win.cpp
    )
elseif(UNIX)
    target_sources(v2_platform PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/osal/mutex/mutex_linux.cpp
    )
endif()

# Compile options
if(WIN32)
    target_compile_options(v2_platform PRIVATE /W4)
elseif(UNIX)
    target_compile_options(v2_platform PRIVATE -Wall -Wextra -Wpedantic)
endif()
