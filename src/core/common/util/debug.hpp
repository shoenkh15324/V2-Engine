#pragma once
#include <string>
#include <cstdlib>
#include <iostream>
#include "core/common/log/log.hpp"
#include "core/common/config/platform_config.h"

#if V2_COMPILER_MSVC
    #define V2_BREAKPOINT() __debugbreak()
#elif V2_COMPILER_GCC || V2_COMPILER_CLANG
    #define V2_BREAKPOINT() __builtin_trap()
#else
    #define V2_BREAKPOINT() ((void)0)
#endif

#ifndef NDEBUG
    #define V2_ASSERT(x, msg) \
        do{ \
            if(!(x)){ \
                V2_LOG_FATAL(msg); \
                std::string box = "\n[ASSERT FAILED]\n"; \
                box += "Message    : " + std::string(msg) + "\n"; \
                box += "Expression : " + std::string(#x) + "\n"; \
                box += "File       : " + std::string(__FILE__) + "\n"; \
                box += "Line       : " + std::to_string(__LINE__) + "\n"; \
                box += "Function   : " + std::string(__func__) + "\n"; \
                activeLogger().logBlock(box); \
                V2_BREAKPOINT(); \
                std::abort(); \
            } \
        }while(0)
#else
    #define V2_ASSERT(x, ...) ((void)0)
#endif

#define V2_PANIC(msg) \
    do{ \
        V2_LOG_FATAL(msg); \
        std::string box = "\n[PANIC]\n"; \
        box += "Message    : " + std::string(msg) + "\n"; \
        box += "File       : " + std::string(__FILE__) + "\n"; \
        box += "Line       : " + std::to_string(__LINE__) + "\n"; \
        box += "Function   : " + std::string(__func__) + "\n"; \
        activeLogger().logBlock(box); \
        V2_BREAKPOINT(); \
        std::abort(); \
    }while(0)

#define V2_UNREACHABLE() V2_PANIC()
