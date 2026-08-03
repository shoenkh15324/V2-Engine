#include "timer_linux.hpp"
#include <cerrno>
#include <unistd.h>
#include <sys/timerfd.h>
#include "core/common/util/return.hpp"

LinuxTimer::LinuxTimer(IEventLoop* eventLoop) : eventLoop_(eventLoop){
    timerFd_ = ::timerfd_create(CLOCK_MONOTONIC, (TFD_NONBLOCK | TFD_CLOEXEC));
}

LinuxTimer::~LinuxTimer(){
    stop();
}

void LinuxTimer::start(){
    if(!subscribed_ && (timerFd_ >= 0 && eventLoop_)){
        subscribed_ = (eventLoop_->subscribe(timerFd_, [this](){ handleTimerEvent(); }) == Ok);
    }
}

void LinuxTimer::stop(){
    if(subscribed_ && eventLoop_ && (timerFd_ >= 0)){
        eventLoop_->unsubscribe(timerFd_);
    }
    subscribed_ = false;
    if(timerFd_ >= 0){
        ::close(timerFd_);
        timerFd_ = -1;
    }
    clear();
}

void LinuxTimer::scheduleNextTimer(const Clock::time_point& now){
    if(timerFd_ < 0) return;
    purgeDeadEntries();
    itimerspec spec{};
    if(hasLiveTop()){
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(nextExpiry() - now).count();
        if(ns < 0){
            spec.it_value.tv_sec = 0;
            spec.it_value.tv_nsec = 1; // 1ns 이후 발화 (즉시 발화)
        }else{
            spec.it_value.tv_sec = ns / 1000000000;
            spec.it_value.tv_nsec = ns % 1000000000;
        }
    }
    ::timerfd_settime(timerFd_, 0, &spec, nullptr); // 빈 힙이면 disarm
}
void LinuxTimer::onWake(){
    if(timerFd_ >= 0){
        uint64_t val;
        ssize_t r;
        do{
            r = ::read(timerFd_, &val, sizeof(val));
        }while((r < 0) && (errno == EINTR));
    }
}
