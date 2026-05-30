#include "log.hpp"
#include <cstdio>
#include <cstdarg>
#include "core/osal/mutex/mutex.hpp"
#include "core/osal/lock_guard/lock_guard.hpp"

namespace core::common{

LogLevel gLevel = LogLevel::Verbose;
osal::Mutex gMutex;

const char* colorCode(LogLevel level){
    switch(level){
        case LogLevel::Error: return "\033[31m"; // red
        case LogLevel::Warn:  return "\033[33m"; // yellow
        case LogLevel::Info:  return "\033[36m"; // cyan
        default:              return "\033[37m"; // white
    }
}

const char* levelLabel(LogLevel level){
    switch(level){
        case LogLevel::Error: return "[ERROR]";
        case LogLevel::Warn:  return "[WARN]";
        case LogLevel::Info:  return "[INFO]";
        default:              return "[LOG]";
    }
}

void setLogLevel(LogLevel level){
    gLevel = level;
}

LogLevel getLogLevel(){
    return gLevel;
}

void logPrint(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...){
    if(level < gLevel) return;
    osal::LockGuard<osal::Mutex> lock(gMutex);
    fprintf(stderr, "%s%s %s:%d (%s) ", colorCode(level), levelLabel(level), file, line, func);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\033[0m\n"); // color reset
    fflush(stderr);
}

} // namespace core::common
