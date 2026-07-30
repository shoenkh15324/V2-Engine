#include "core/common/log/log.hpp"
#include <mutex>
#include <cstdio>
#include <atomic>
#include <chrono>

namespace{
    thread_local std::string gBuf;
    FILE* gLogFile = nullptr;
    std::atomic<LogLevel> gLevel{LogLevel::Info};
    std::mutex gMutex;
    std::string gAppName = "";
}

void setLogLevel(LogLevel level){ gLevel.store(level, std::memory_order_relaxed); }

LogLevel getLogLevel(){ return gLevel.load(std::memory_order_relaxed); }

void setLogAppName(std::string_view name){ gAppName = name; }

void setLogFile(std::string_view path){
    std::lock_guard lock(gMutex);
    if(gLogFile) fclose(gLogFile);
    gLogFile = path.empty() ? nullptr : fopen(path.data(), "w");
}

void logFlush(){
    if(gBuf.empty()) return;
    fwrite(gBuf.data(), 1, gBuf.size(), stderr);
    {
        std::lock_guard lock(gMutex);
        if(gLogFile) fwrite(gBuf.data(), 1, gBuf.size(), gLogFile);
    }
    gBuf.clear();
}

void logPrint(LogLevel level, const char* file, int line, const char* func, std::string msg){
    if(level < gLevel.load(std::memory_order_relaxed)) return;
    auto now = std::chrono::system_clock::now();
    std::format_to(std::back_inserter(gBuf), "[{:%Y-%m-%dT%H:%M:%S}]", now);
    switch(level){
        case LogLevel::Error: gBuf += "\033[31m[ERROR]\033[0m "; break;
        case LogLevel::Warn:  gBuf += "\033[33m[WARN]\033[0m ";  break;
        case LogLevel::Info:  gBuf += "\033[36m[INFO]\033[0m ";  break;
        default:              gBuf += "\033[37m[LOG]\033[0m ";   break;
    }
    std::format_to(std::back_inserter(gBuf), "{}:{} ({}) ", file, line, func);
    gBuf += msg;
    gBuf += '\n';
    if(gBuf.size() >= 4096) logFlush();
}

namespace{ auto _ = (atexit(logFlush), 0); }
