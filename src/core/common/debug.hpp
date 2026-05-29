#pragma once
#include <cstdlib>
#include <iostream>

#if defined(_MSC_VER)
    #define CORE_BREAKPOINT() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
    #define CORE_BREAKPOINT() __builtin_trap()
#else
    #define CORE_BREAKPOINT() ((void)0)
#endif

#ifndef NDEBUG
    #define CORE_ASSERT(x) \
        do{ \
            if(!(x)){ \
                std::cerr \
                    << "\n[ASSERT FAILED]\n" \
                    << "Expression : " << #x << "\n" \
                    << "File       : " << __FILE__ << "\n" \
                    << "Line       : " << __LINE__ << "\n" \
                    << "Function   : " << __func__ << "\n" \
                    << std::endl; \
                CORE_BREAKPOINT(); \
                std::abort(); \
            } \
        }while(0)
#else
    #define CORE_ASSERT(x) ((void)0)
#endif

#define CORE_PANIC(msg) \
    do{ \
        std::cerr \
            << "\n[PANIC]\n" \
            << "Message    : " << msg << "\n" \
            << "File       : " << __FILE__ << "\n" \
            << "Line       : " << __LINE__ << "\n" \
            << "Function   : " << __func__ << "\n" \
            << std::endl; \
        CORE_BREAKPOINT(); \
        std::abort(); \
    }while(0)

#define CORE_UNREACHABLE() CORE_PANIC()
