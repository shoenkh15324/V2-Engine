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
        V2_LOG_ERROR("timerfd_create() failed, errno={}", errno);
    }
#endif
}

Timer::~Timer(){
    stop();
}

int Timer::add(uint64_t delayMs, bool repeating, Callback cb, void* payload){
    auto now = Clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    int idx = allocNode();
    auto& node = pool_[idx];
    node.id = nextId_++;
    node.expiry = now + std::chrono::milliseconds(delayMs);
    node.interval = std::chrono::milliseconds(delayMs);
    node.repeating = repeating;
    node.cb = cb;
    node.payload = payload;
    bool needReschedule = heap_.empty() || (node.expiry < pool_[heap_.top()].expiry);
    heap_.push(idx);
    timers_[node.id] = idx;
    if(needReschedule) scheduleNextTimer(now);
    return node.id;
}

void Timer::cancel(int id){
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(id);
    if(it == timers_.end()) return;
    int idx = it->second;
    pool_[idx].alive = false;
    timers_.erase(it);
    while(!heap_.empty() && !pool_[heap_.top()].alive){
        heap_.pop();
    }
}

void Timer::clear(){
    std::lock_guard<std::mutex> lock(mutex_);
    heap_ = decltype(heap_){compare_};
    pool_.clear();
    freeList_.clear();
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
            while(!heap_.empty() && !pool_[heap_.top()].alive){
                heap_.pop();
            }
            if(heap_.empty()){
                cv_.wait(lock, [this]{ return !running_ || !heap_.empty(); });
                continue;
            }
            auto now = Clock::now();
            if(pool_[heap_.top()].expiry > now){
                cv_.wait_until(lock, pool_[heap_.top()].expiry);
                continue;
            }
            lock.unlock();
            executeExpiredTimers();
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
    executeExpiredTimers();
}

bool Timer::isRepeating(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(id);
    if(it == timers_.end()) return false;
    return pool_[it->second].repeating;
}

bool Timer::isAlive(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(id);
    return it != timers_.end() && pool_[it->second].alive;
}

int Timer::allocNode(){
    if(!freeList_.empty()){
        int idx = freeList_.back();
        freeList_.pop_back();
        pool_[idx] = TimerNode{};
        return idx;
    }
    pool_.emplace_back();
    return static_cast<int>(pool_.size()) - 1;
}

void Timer::freeNode(int idx){
    pool_[idx] = TimerNode{};
    freeList_.push_back(idx);
}

void Timer::executeExpiredTimers(){
    struct ReadyNode{
        int64_t id;
        Callback cb;
        void* payload;
    };
    std::vector<ReadyNode> ready; // 인덱스 목록
    auto now = Clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while(!heap_.empty() && (pool_[heap_.top()].expiry <= now)){
            int idx = heap_.top();
            heap_.pop();
            if(!pool_[idx].alive) continue;
            ready.push_back({pool_[idx].id, pool_[idx].cb, pool_[idx].payload});
        }
    }
    for(auto& rn : ready){ // 콜백: pool과 무관한 로컬 데이터로 호출
        rn.cb(rn.id, rn.payload);
    }
    { // 재조회: id로 timers_ lookup (cancel/add로 인한 pool 재할당 안전)
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& rn : ready){
            auto it = timers_.find(rn.id);
            if(it == timers_.end()) continue; // 콜백 중에 cancel됨
            int idx = it->second;
            if(pool_[idx].repeating && pool_[idx].alive){
                pool_[idx].expiry += pool_[idx].interval;
                heap_.push(idx);
            } else {
                timers_.erase(rn.id);
                freeNode(idx);
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
        while(!heap_.empty() && !pool_[heap_.top()].alive){
            heap_.pop();
        }
        if(!heap_.empty()){
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(pool_[heap_.top()].expiry - now).count();
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
