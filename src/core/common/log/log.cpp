#include "core/common/log/log.hpp"
#include <mutex>
#include <cstdio>
#include <atomic>
#include <chrono>

namespace{
    static constexpr int kLogFlushBufSize = 512;

    FILE* gLogFile = nullptr;
    std::atomic<LogLevel> gLevel{LogLevel::Info};
    std::mutex gMutex;
    std::string gAppName = "";

    struct LogBuffer {
        std::string data;

        void flush(){
            if(data.empty()) return;
            fwrite(data.data(), 1, data.size(), stderr);
            {
                std::lock_guard lock(gMutex);
                if(gLogFile){
                    fwrite(data.data(), 1, data.size(), gLogFile);
                    fflush(gLogFile);
                }
            }
            data.clear();
        }

        ~LogBuffer(){ flush(); }
    };
    thread_local LogBuffer gBuf;
}

void setLogLevel(LogLevel level){ gLevel.store(level, std::memory_order_relaxed); }

LogLevel getLogLevel(){ return gLevel.load(std::memory_order_relaxed); }

void setLogAppName(std::string_view name){ gAppName = name; }

void setLogFile(std::string_view path){
    std::lock_guard lock(gMutex);
    if(gLogFile) fclose(gLogFile);
    gLogFile = path.empty() ? nullptr : fopen(path.data(), "w");
}

void logPrint(LogLevel level, const char* file, int line, const char* func, std::string msg){
    if(level < gLevel.load(std::memory_order_relaxed)) return;
    auto now = std::chrono::system_clock::now();
    std::format_to(std::back_inserter(gBuf.data), "[{:%Y-%m-%dT%H:%M:%S}]", now);
    switch(level){
        case LogLevel::Error: gBuf.data += "\033[31m[ERROR]\033[0m "; break;
        case LogLevel::Warn:  gBuf.data += "\033[33m[WARN]\033[0m ";  break;
        case LogLevel::Info:  gBuf.data += "\033[36m[INFO]\033[0m ";  break;
        default:              gBuf.data += "\033[37m[LOG]\033[0m ";   break;
    }
    std::format_to(std::back_inserter(gBuf.data), "{}:{} ({}) ", file, line, func);
    gBuf.data += msg;
    gBuf.data += '\n';
    if(gBuf.data.size() >= kLogFlushBufSize) gBuf.flush();
}
