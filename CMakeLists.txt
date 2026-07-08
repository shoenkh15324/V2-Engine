cmake_minimum_required(VERSION 3.14.0)
project(V2_Engine VERSION 0.7.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=gold" CACHE STRING "" FORCE)
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache enabled")
endif()

message(STATUS "Configuring Root")

# ── External dependencies ──────────────────────────────────
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

FetchContent_Declare(sdbuscpp
    GIT_REPOSITORY https://github.com/Kistler-Group/sdbus-cpp.git
    GIT_TAG v2.3.1
)
set(SDBUSCPP_BUILD_CODEGEN OFF)
set(SDBUSCPP_BUILD_EXAMPLES OFF)
set(SDBUSCPP_BUILD_TESTS OFF)
set(SDBUSCPP_BUILD_DOCS OFF)
set(SDBUSCPP_INSTALL OFF)
FetchContent_MakeAvailable(sdbuscpp)
if(NOT TARGET SDBusCpp::sdbus-c++)
    add_library(SDBusCpp::sdbus-c++ ALIAS sdbus-c++)
endif()

# ── Internal modules ───────────────────────────────────────
include(${CMAKE_CURRENT_LIST_DIR}/src/core/core.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/src/infra/infra.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/src/service/service.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/src/app/app.cmake)

# ── Build type ─────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Global compile options ─────────────────────────────────
if(WIN32)
    add_compile_options(-W4)
elseif(UNIX)
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Sanitizers (Debug only) ────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ── LTO (Release only) ─────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _lto_supported)
    if(_lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "LTO enabled")
    endif()
endif()

# ── Testing ────────────────────────────────────────────────
option(BUILD_TESTING "Build Tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

# ── Packaging ──────────────────────────────────────────────
#set(CPACK_PROJECT_NAME ${PROJECT_NAME})
#set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
#include(CPack)
