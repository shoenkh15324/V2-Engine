#pragma once
#include <mutex>
#include <string>
#include <format>
#include <atomic>
#include <cstdint>
#include <string_view>

enum class LogLevel : uint8_t {
    Verbose = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

class Logger {
public:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    void log(LogLevel level, std::string_view file, int line, std::string_view func, std::string_view msg);
    void logBlock(std::string_view text); // 원본 그대로 stderr + logFile에 기록 후 flush (크래시/팬릭 박스용)
    void setLevel(LogLevel level){ level_.store(level, std::memory_order_relaxed); }
    LogLevel getLevel() const { return level_.load(std::memory_order_relaxed); }
    void setLogFile(std::string_view path);
    void flushBuffer(); // thread_local 버퍼 flush (LogBuffer dtor에서 호출 — 내부용이지만 public)

private:
    FILE* logFile_ = nullptr;
    std::mutex mutex_;
    std::atomic<LogLevel> level_{LogLevel::Info};
};

Logger& activeLogger();
void setActiveLogger(Logger* logger);
void clearActiveLogger();

#define V2_LOGGER() (&activeLogger())
#define V2_LOG_VERBOSE(...) activeLogger().log(LogLevel::Verbose, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
#define V2_LOG_DEBUG(...) activeLogger().log(LogLevel::Debug, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
#define V2_LOG_INFO(...) activeLogger().log(LogLevel::Info, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
#define V2_LOG_WARN(...) activeLogger().log(LogLevel::Warn, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
#define V2_LOG_ERROR(...) activeLogger().log(LogLevel::Error, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__));
#define V2_LOG_FATAL(...) \
    do{ \
        activeLogger().log(LogLevel::Fatal, __FILE__, __LINE__, __func__, std::format(__VA_ARGS__)); \
        activeLogger().flushBuffer(); \
    }while(0)
