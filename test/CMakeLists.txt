# ── External dependencies ──────────────────────────────────
include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.2
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

include(GoogleTest)

# ── Internal modules ───────────────────────────────────────
include(${CMAKE_CURRENT_LIST_DIR}/unit/unit_test.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/integration/integration_test.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/benchmark/benchmark.cmake)
