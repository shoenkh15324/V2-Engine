#pragma once

// ============================================================================
// Platform Detection
// ============================================================================
#if defined(_WIN32)
    #define V2_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define V2_PLATFORM_LINUX 1
#elif defined(__APPLE__)
    #define V2_PLATFORM_MACOS 1
#else
    #error "V2 Engine: Unsupported platform"
#endif

// ============================================================================
// Compiler Detection
// ============================================================================
#if defined(_MSC_VER)
    #define V2_COMPILER_MSVC 1
#elif defined(__clang__)
    #define V2_COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define V2_COMPILER_GCC 1
#endif

// ============================================================================
// Platform Abstraction
// ============================================================================
#if V2_PLATFORM_WINDOWS
    using ConnHandle = uintptr_t;
#else
    using ConnHandle = int;
#endif
