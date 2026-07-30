#pragma once
#include <string>
#include <format>
#include <cstdint>
#include <string_view>

enum class LogLevel : uint8_t {
    Verbose = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

void setLogLevel(LogLevel level);
LogLevel getLogLevel();
void setLogFile(std::string_view path);
void setLogAppName(std::string_view name);
void logPrint(LogLevel level, const char* file, int line, const char* func, std::string msg);

#define V2_LOG(...) logPrint(LogLevel::Verbose, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
#define V2_LOG_INFO(...) logPrint(LogLevel::Info, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
#define V2_LOG_WARN(...) logPrint(LogLevel::Warn, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
#define V2_LOG_ERROR(...) logPrint(LogLevel::Error, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
