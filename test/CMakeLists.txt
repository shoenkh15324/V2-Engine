# ── External dependencies ──────────────────────────────────
include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ── Internal modules ───────────────────────────────────────
include(${CMAKE_CURRENT_LIST_DIR}/unit/core/core_test.cmake)
