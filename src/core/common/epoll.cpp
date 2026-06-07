#include "epoll.hpp"
#include "core/common/log.hpp"
#include "core/common/return.hpp"
#include <unistd.h>
#include <cerrno>

Epoll::Epoll(){
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    if(epollFd_ < 0){ 
        V2_LOG_ERROR("epoll_create1() failed");
    }
}

Epoll::~Epoll(){
    if(epollFd_ >= 0){
        ::close(epollFd_);
    }
}

Epoll::Epoll(Epoll&& other) noexcept : epollFd_(other.epollFd_) {
    other.epollFd_ = -1;
}
Epoll& Epoll::operator=(Epoll&& other) noexcept {
    if(this != &other){
        if(epollFd_ >= 0){
            ::close(epollFd_);
        }
        epollFd_ = other.epollFd_;
        other.epollFd_ = -1;
    }
    return *this;
}

int Epoll::add(int fd, uint32_t events, void* ptr){
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = ptr;
    if(epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0){ V2_LOG_ERROR("epoll_ctl ADD fd=%d failed, errno=%d", fd, errno);
        return Fail;
    }
    return Ok;
}

int Epoll::mod(int fd, uint32_t events, void* ptr){
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = ptr;

    if(epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) < 0){ V2_LOG_ERROR("epoll_ctl MOD fd=%d failed, errno=%d", fd, errno);
        return Fail;
    }
    return Ok;
}

int Epoll::del(int fd){
    if(epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) < 0){ V2_LOG_ERROR("epoll_ctl DEL fd=%d failed, errno=%d", fd, errno);
        return Fail;
    }
    return Ok;
}

int Epoll::wait(epoll_event* events, int maxEvents, int timeoutMs){
    int n;
    do{
        n = epoll_wait(epollFd_, events, maxEvents, timeoutMs);
    }while(n < 0 && errno == EINTR);
    if(n < 0){ V2_LOG_ERROR("epoll_wait() failed, errno=%d", errno);
        return Fail;
    }
    return n; // number of ready FDs
}
