#include "core/common/time/timer.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/log/log.hpp"
#include <cstring>
#include <cerrno>

#if V2_PLATFORM_LINUX
    #include <sys/timerfd.h>
    #include <unistd.h>
#else
    #include <thread>
    #include <condition_variable>
#endif

Timer::Timer(){
#if V2_PLATFORM_LINUX
    timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(timerFd_ < 0){
        V2_LOG_ERROR("timerfd_create() failed, errno=%d", errno);
    }
#endif
}

Timer::~Timer(){
    stop();
}

int Timer::add(uint64_t delayMs, bool repeating, Callback cb){
    auto timerNode = std::make_shared<TimerNode>();
    auto now = Clock::now();
    timerNode->id = nextId_++;
    timerNode->expiry = now + std::chrono::milliseconds(delayMs);
    timerNode->interval = std::chrono::milliseconds(delayMs);
    timerNode->repeating = repeating;
    timerNode->cb = std::move(cb);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool needReschedule = heap_.empty() || (timerNode->expiry < heap_.top()->expiry);
        heap_.push(timerNode);
        timers_[timerNode->id] = timerNode;
        if(needReschedule) scheduleNextTimer(now);
    }
    return timerNode->id;
}

void Timer::cancel(int id){
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(id);
    if(it == timers_.end()) return;
    it->second->alive = false;
    timers_.erase(it);
    while(!heap_.empty() && !heap_.top()->alive){
        heap_.pop();
    }
}

void Timer::clear(){
    std::lock_guard<std::mutex> lock(mutex_);
    heap_ = {};
    timers_.clear();
    scheduleNextTimer(Clock::now());
}

void Timer::start(){
#if V2_PLATFORM_LINUX
    //
#else
    running_ = true;
    thread_ = std::thread([this]{
    #if V2_PLATFORM_LINUX
        pthread_setname_np(pthread_self(), "v2-timer");
    #elif V2_PLATFORM_MACOS
        pthread_setname_np("v2-timer");
    #endif
        while(running_){
            std::unique_lock<std::mutex> lock(mutex_);
            while(!heap_.empty() && !heap_.top()->alive){
                heap_.pop();
            }
            if(heap_.empty()){
                cv_.wait(lock, [this]{ return !running_ || !heap_.empty(); });
                continue;
            }
            auto now = Clock::now();
            if(heap_.top()->expiry > now){
                cv_.wait_until(lock, heap_.top()->expiry);
                continue;
            }
            lock.unlock();
            excuteExpiredTimers();
        }
    });
#endif
}

void Timer::stop(){
#if V2_PLATFORM_LINUX
    if(timerFd_ >= 0){
        ::close(timerFd_);
        timerFd_ = -1;
    }
    clear();
#else
    running_ = false;
    cv_.notify_all();
    if(thread_.joinable()){
        thread_.join();
    }
    clear();
#endif
}

int Timer::fd() const {
#if V2_PLATFORM_LINUX
    return timerFd_;
#else
    return -1;
#endif
}

void Timer::handleTimerEvent(){
#if V2_PLATFORM_LINUX
    if(timerFd_ >= 0){
        uint64_t val;
        ssize_t r;
        do{
            r = ::read(timerFd_, &val, sizeof(val));
        }while(r < 0 && errno == EINTR);
    }
#endif
    excuteExpiredTimers();
}

void Timer::excuteExpiredTimers(){
    std::vector<TimerPtr> ready;
    auto now = Clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while(!heap_.empty() && (heap_.top()->expiry <= now)){
            auto timerNode = heap_.top();
            heap_.pop();
            if(!timerNode->alive) continue;
            ready.push_back(timerNode);
        }
    }
    for(auto& timerNode : ready){ timerNode->cb(timerNode->id); }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& timerNode : ready){
            if(timerNode->repeating && timerNode->alive){
                timerNode->expiry += timerNode->interval;
                heap_.push(timerNode);
            }else{
                timers_.erase(timerNode->id);
            }
        }
        scheduleNextTimer(now);
    }
}

void Timer::scheduleNextTimer(const Clock::time_point& now){
#if V2_PLATFORM_LINUX
    if(timerFd_ < 0) return;
    itimerspec spec{};
    if(!heap_.empty()){
        while(!heap_.empty() && !heap_.top()->alive){
            heap_.pop();
        }
        if(!heap_.empty()){
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(heap_.top()->expiry - now).count();
            if(ns < 0){
                spec.it_value.tv_sec = 0;
                spec.it_value.tv_nsec = 1;
            }else{
                spec.it_value.tv_sec = ns / 1000000000;
                spec.it_value.tv_nsec = ns % 1000000000;
            }
        }
    }
    timerfd_settime(timerFd_, 0, &spec, nullptr);
#else
    cv_.notify_one();
#endif
}
