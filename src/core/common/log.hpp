#pragma once
#include <cstdint>

enum class LogLevel{
    Verbose = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

void setLogLevel(LogLevel level);
LogLevel getLogLevel();
void logPrint(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...);

#define V2_LOG(...) logPrint(LogLevel::Verbose, __FILE__, __LINE__, __func__, __VA_ARGS__);
#define V2_LOG_INFO(...) logPrint(LogLevel::Info, __FILE__, __LINE__, __func__, __VA_ARGS__);
#define V2_LOG_WARN(...) logPrint(LogLevel::Warn, __FILE__, __LINE__, __func__, __VA_ARGS__);
#define V2_LOG_ERROR(...) logPrint(LogLevel::Error, __FILE__, __LINE__, __func__, __VA_ARGS__);
