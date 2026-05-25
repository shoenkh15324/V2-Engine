cmake_minimum_required(VERSION 3.10.0)
project(V2Engine VERSION 0.1.0 LANGUAGES C CXX)

add_library(v2_core
    src/core/common/buffer.cpp
    src/core/common/log.cpp
    src/core/common/time.cpp
    src/core/common/debug.cpp
    src/core/actor/actor.cpp
    src/core/runtime/scheduler.cpp
    src/core/runtime/runtime.cpp
    src/core/runtime/worker.cpp
    src/core/messaging/mailbox.cpp
    src/platform/osal/thread.cpp
    src/platform/osal/mutex.cpp
    src/platform/osal/semaphore.cpp
    src/platform/osal/timer.cpp
    src/platform/osal/sleep.cpp
)

target_include_directories(v2_core PUBLIC
    src/
)

# demo app
add_executable(v2_demo
    src/app/demo/main.cpp
    src/app/demo/demo_app.cpp
)

target_link_libraries(v2_demo PRIVATE v2_core)

# cli app
add_executable(v2_cli
    src/app/cli/main.cpp
    src/app/cli/cli_app.cpp
)

target_link_libraries(v2_cli PRIVATE v2_core)

if(MSVC)
    target_compile_options(v2_core PRIVATE /W4) 
else()
    target_compile_options(v2_core PRIVATE -Wall -Wextra -Wpedantic) 
endif()

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
