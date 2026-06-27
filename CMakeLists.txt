cmake_minimum_required(VERSION 3.10.0)
project(V2_Engine VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "" FORCE)

message(STATUS "Configuring Root")

include(${CMAKE_CURRENT_LIST_DIR}/src/core/core.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/src/infra/infra.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/src/service/service.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/src/app/app.cmake)

# Fetch
include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v7.0.0
)
FetchContent_MakeAvailable(ftxui)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)

# Build Type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compile options
if(WIN32)
    target_compile_options(v2_core PRIVATE /W4)
elseif(UNIX)
    target_compile_options(v2_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
