#include "timer.hpp"

Timer::~Timer(){
    stop();
}

void Timer::start(){
    std::lock_guard<std::mutex> lock(mutex_);
    if(thread_.joinable()) return;
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this]{ runLoop(); });
}

void Timer::stop(){
    running_.store(false, std::memory_order_release);
    sema_.release();
    if(thread_.joinable() && (std::this_thread::get_id() != thread_.get_id())){
        thread_.join();
    }
    clear();
}

void Timer::scheduleNextTimer(const Clock::time_point& now){
    sema_.release();
}

void Timer::runLoop(){
    while(running_.load(std::memory_order_relaxed)){
        std::unique_lock<std::mutex> lock(mutex_);
        purgeDeadEntries();
        if(!hasLiveTop()){
            lock.unlock();
            sema_.acquire(); // 알림 올 때까지 블록 
            continue;
        }
        auto expiry = nextExpiry();
        lock.unlock();
        if(sema_.try_acquire_until(expiry)) continue; // 스케줄 변경 알림 -> 재스케쥴링
        executeExpiredTimers(); // 만료
    }
}
