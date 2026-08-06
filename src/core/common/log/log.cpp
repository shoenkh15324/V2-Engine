#include "core/common/log/log.hpp"
#include <chrono>

namespace {
    static constexpr int kLogFlushBufSize = 512;

    std::atomic<Logger*> gLogger{nullptr};

    Logger& fallbackLogger(){
        static Logger fallback;
        return fallback;
    };

    struct LogBuffer {
        std::string data;
        ~LogBuffer(){
            if(!data.empty()){ activeLogger().flushBuffer(); }
        }
    };
    
    thread_local LogBuffer gBuf;
}; // namespace

Logger& activeLogger(){
    Logger* logger = gLogger.load(std::memory_order_relaxed);
    return logger ? *logger : fallbackLogger();
}

void setActiveLogger(Logger* logger){ gLogger.store(logger, std::memory_order_relaxed); }

void clearActiveLogger(){ gLogger.store(nullptr, std::memory_order_relaxed); }

Logger::~Logger(){
    std::lock_guard lock(mutex_);
    if(logFile_) fclose(logFile_);
}

void Logger::log(LogLevel level, std::string_view file, int line, std::string_view func, std::string_view msg){
    if(level < level_.load(std::memory_order_relaxed)) return;
    auto now = std::chrono::system_clock::now();
    std::format_to(std::back_inserter(gBuf.data), "[{:%Y-%m-%dT%H:%M:%S}]", now);
    switch(level){
        case LogLevel::Verbose: gBuf.data += "\033[90m[VERBOSE]\033[0m ";   break;
        case LogLevel::Debug:   gBuf.data += "\033[37m[DEBUG]\033[0m ";     break;
        case LogLevel::Info:    gBuf.data += "\033[36m[INFO]\033[0m ";      break;
        case LogLevel::Warn:    gBuf.data += "\033[33m[WARN]\033[0m ";      break;
        case LogLevel::Error:   gBuf.data += "\033[31m[ERROR]\033[0m ";     break;
        case LogLevel::Fatal:   gBuf.data += "\033[91m[FATAL]\033[0m ";     break;
        default:                gBuf.data += "\033[90m[LOG]\033[0m ";       break;
    }
    std::format_to(std::back_inserter(gBuf.data), "{}:{} ({}) ", file, line, func);
    gBuf.data += msg;
    gBuf.data += '\n';
    if(gBuf.data.size() >= kLogFlushBufSize) flushBuffer();
}

void Logger::setLogFile(std::string_view path){
    std::lock_guard lock(mutex_);
    if(logFile_) fclose(logFile_);
    logFile_ = path.empty() ? nullptr : fopen(path.data(), "w");
}

void Logger::logBlock(std::string_view text){
    std::lock_guard lock(mutex_);
    fwrite(text.data(), 1, text.size(), stderr);
    if(logFile_){
        fwrite(text.data(), 1, text.size(), logFile_);
        fflush(logFile_);
    }
}

void Logger::flushBuffer(){
    std::lock_guard lock(mutex_);
    fwrite(gBuf.data.data(), 1, gBuf.data.size(), stderr);
    if(logFile_){
        fwrite(gBuf.data.data(), 1, gBuf.data.size(), logFile_);
        fflush(logFile_);
    }
    gBuf.data.clear();
}
