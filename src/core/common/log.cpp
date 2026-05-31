#include "log.hpp"
#include <cstdio>
#include <cstdarg>
#include <mutex>

LogLevel gLevel = LogLevel::Verbose;
std::mutex gMutex;

const char* colorCode(LogLevel level){
    switch(level){
        case LogLevel::Error: return "\033[31m";
        case LogLevel::Warn:  return "\033[33m";
        case LogLevel::Info:  return "\033[36m";
        default:              return "\033[37m";
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
    std::lock_guard<std::mutex> lock(gMutex);
    fprintf(stderr, "%s%s %s:%d (%s) ", colorCode(level), levelLabel(level), file, line, func);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\033[0m\n");
    fflush(stderr);
}
