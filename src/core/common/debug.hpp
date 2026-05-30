#pragma once
#include <cstdlib>
#include <iostream>

#if defined(_MSC_VER)
    #define V2_BREAKPOINT() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
    #define V2_BREAKPOINT() __builtin_trap()
#else
    #define V2_BREAKPOINT() ((void)0)
#endif

#ifndef NDEBUG
    #define V2_ASSERT(x) \
        do{ \
            if(!(x)){ \
                std::cerr \
                    << "\n[ASSERT FAILED]\n" \
                    << "Expression : " << #x << "\n" \
                    << "File       : " << __FILE__ << "\n" \
                    << "Line       : " << __LINE__ << "\n" \
                    << "Function   : " << __func__ << "\n" \
                    << std::endl; \
                V2_BREAKPOINT(); \
                std::abort(); \
            } \
        }while(0)
#else
    #define V2_ASSERT(x) ((void)0)
#endif

#define V2_PANIC(msg) \
    do{ \
        std::cerr \
            << "\n[PANIC]\n" \
            << "Message    : " << msg << "\n" \
            << "File       : " << __FILE__ << "\n" \
            << "Line       : " << __LINE__ << "\n" \
            << "Function   : " << __func__ << "\n" \
            << std::endl; \
        V2_BREAKPOINT(); \
        std::abort(); \
    }while(0)

#define V2_UNREACHABLE() V2_PANIC()
