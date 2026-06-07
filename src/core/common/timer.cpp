#include "timer.hpp"
#include "core/common/log.hpp"
#include <cstring>

#ifdef __linux__
    #include <sys/timerfd.h>
    #include <unistd.h>
    #include <cerrno>
#else
    #include <thread>
    #include <condition_variable>
#endif

Timer::Timer(){
#ifdef __linux__
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
    timerNode->id = nextId_++;
    timerNode->expiry = Clock::now() + std::chrono::milliseconds(delayMs);
    timerNode->interval = std::chrono::milliseconds(delayMs);
    timerNode->repeating = repeating;
    timerNode->cb = std::move(cb);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        heap_.push(timerNode);
        timers_[timerNode->id] = timerNode;
        rearm();
    }
    return timerNode->id;
}

void Timer::cancel(int id){
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timers_.find(id);
    if(it == timers_.end()) return;
    it->second->alive = false;
    timers_.erase(it);
}

void Timer::clear(){
    std::lock_guard<std::mutex> lock(mutex_);
    heap_ = {};
    timers_.clear();
    rearm();
}

void Timer::start(){
#ifdef __linux__
    //
#else
    running_ = true;
    thread_ = std::thread([this]{
        pthread_setname_np(pthread_self(), "v2-timer");
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
            fire();
        }
    });
#endif
}

void Timer::stop(){
#ifdef __linux__
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
#ifdef __linux__
    return timerFd_;
#else
    return -1;
#endif
}

void Timer::onTick(){
#ifdef __linux__
    if(timerFd_ >= 0){
        uint64_t val;
        ssize_t r;
        do{
            r = ::read(timerFd_, &val, sizeof(val));
        }while(r < 0 && errno == EINTR);
    }
#endif
    fire();
}

void Timer::fire(){
    std::vector<TimerPtr> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = Clock::now();
        while(!heap_.empty() && heap_.top()->expiry <= now){
            auto timerNode = heap_.top();
            heap_.pop();
            if(!timerNode->alive) continue;
            ready.push_back(timerNode);
        }
    }
    for(auto& timerNode : ready){
        timerNode->cb(timerNode->id);
    }
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
        rearm();
    }
}

void Timer::rearm(){
#ifdef __linux__
    if(timerFd_ < 0) return;
    itimerspec spec{};
    if(!heap_.empty()){
        while(!heap_.empty() && !heap_.top()->alive){
            heap_.pop();
        }
        if(!heap_.empty()){
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(heap_.top()->expiry - Clock::now()).count();
            if(ns < 0){
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
