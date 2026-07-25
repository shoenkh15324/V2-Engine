#pragma once
#include <cstddef>

#if defined(__APPLE__) && defined(__arm64__)
    inline constexpr size_t kCacheLine = 128;
#elif defined(__aarch64__)
    inline constexpr size_t kCacheLine = 64;
#else
    inline constexpr size_t kCacheLine = 64;
#endif
