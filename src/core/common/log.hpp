#pragma once
#include <cstdint>

namespace core::common{

enum class LogLevel{
    Verbose = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

void setLogLevel(LogLevel level);
LogLevel getLogLevel();
void logPrint(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...);

} // namespace core::common

#define V2_LOG(...) core::common::logPrint(core::common::LogLevel::Verbose, __FILE__, __LINE__, __func__, __VA_ARGS__);
#define V2_LOG_INFO(...) core::common::logPrint(core::common::LogLevel::Info, __FILE__, __LINE__, __func__, __VA_ARGS__);
#define V2_LOG_WARN(...) core::common::logPrint(core::common::LogLevel::Warn, __FILE__, __LINE__, __func__, __VA_ARGS__);
#define V2_LOG_ERROR(...) core::common::logPrint(core::common::LogLevel::Error, __FILE__, __LINE__, __func__, __VA_ARGS__);
