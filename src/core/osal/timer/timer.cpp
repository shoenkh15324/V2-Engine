#include "timer.hpp"

namespace core::osal{

Timer::~Timer(){
    close();
}

int Timer::open(int periodMs){
    if(periodMs <= 0) return -1;
#if defined(__linux__)
    fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(fd_ < 0) return -1;
    itimerspec ts{};
    ts.it_value.tv_sec  = periodMs / 1000;
    ts.it_value.tv_nsec = (periodMs % 1000) * 1000000;
    ts.it_interval = ts.it_value;
    if(timerfd_settime(fd_, 0, &ts, nullptr) < 0){
        ::close(fd_);
        fd_ = -1;
        return -1;
    }
#elif defined(_WIN32)
    h_ = CreateWaitableTimer(nullptr, FALSE, nullptr);
    if(!h_) return -1;
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -(static_cast<LONGLONG>(periodMs) * 10000);
    if(!SetWaitableTimer(h_, &dueTime, periodMs, nullptr, nullptr, FALSE)){
        CloseHandle(h_);
        h_ = nullptr;
        return -1;
    }
#endif
    return 0;
}

int Timer::close(){
#if defined(__linux__)
    if(fd_ >= 0){
        itimerspec ts{};
        timerfd_settime(fd_, 0, &ts, nullptr);
        ::close(fd_);
        fd_ = -1;
    }
#elif defined(_WIN32)
    if(h_){
        CancelWaitableTimer(h_);
        CloseHandle(h_);
        h_ = nullptr;
    }
#endif
    return 0;
}

} // namespace core::osal
