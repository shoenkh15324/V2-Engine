#pragma once
#include <cstdint>
#include <string_view>

enum class LogLevel : uint8_t {
    Verbose = 0,
    Info = 1,
    Warn = 2,
    Error = 3
};

class ILogger {
public:
    virtual ~ILogger() = default;

    virtual void log(LogLevel level, std::string_view file, int line, std::string_view func, std::string_view msg) = 0;
    virtual void setLevel(LogLevel level) = 0;
    virtual LogLevel getLevel() const = 0;
    virtual void setOutputFile(std::string_view path) = 0;
};
