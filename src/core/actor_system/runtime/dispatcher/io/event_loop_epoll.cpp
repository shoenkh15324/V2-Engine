#include "event_loop_epoll.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/debug.hpp"
#include "core/common/util/return.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>

EventLoopEpoll::EventLoopEpoll(int maxEvents, int waitTimeoutMs)
    : maxEvents_(maxEvents), waitTimeoutMs_(waitTimeoutMs), epollEvents_(maxEvents){}

EventLoopEpoll::~EventLoopEpoll(){
    if(stopFd_ >= 0) ::close(stopFd_);
}

void EventLoopEpoll::start(){
    V2_ASSERT(epoll_.fd() >= 0, "EventLoopEpoll needs a valid epoll fd");
    stopFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    V2_ASSERT(stopFd_ >= 0, "eventfd() failed");
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = stopFd_;
    int result = epoll_ctl(epoll_.fd(), EPOLL_CTL_ADD, stopFd_, &ev);
    V2_ASSERT(result >= 0, "epoll_ctl ADD stopFd failed");
    running_.store(true, std::memory_order_release);
}

void EventLoopEpoll::run(){
    pthread_setname_np(pthread_self(), "v2-main");
    threadId_ = std::this_thread::get_id();

    while(running_.load(std::memory_order_relaxed)){
        drainPendingOps(); // epoll_wait 전에 pending ops 처리
        int n = epoll_.wait(epollEvents_.data(), maxEvents_, waitTimeoutMs_);
        if(n < 0){
            if(errno == EINTR) continue;
            break;
        }
        for(int i = 0; i < n; i++){
            if(epollEvents_[i].data.fd == stopFd_){
                uint64_t val;
                ssize_t r;
                do{
                    r = ::read(stopFd_, &val, sizeof(val));
                }while(r < 0 && errno == EINTR);
                continue;
            }
            WatchedFd fd = epollEvents_[i].data.fd;
            auto it = handlers_.find(fd);
            if(it != handlers_.end()){
                it->second();
            }
        }
    }
}

void EventLoopEpoll::stop(){
    running_.store(false, std::memory_order_release);
    uint64_t one = 1;
    auto _ = ::write(stopFd_, &one, sizeof(one));
    (void)_;
}

int EventLoopEpoll::subscribe(WatchedFd fd, Handler handler){
    if(std::this_thread::get_id() == threadId_){
        handlers_[fd] = std::move(handler);
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if(epoll_ctl(epoll_.fd(), EPOLL_CTL_ADD, fd, &ev) < 0){
            handlers_.erase(fd);
            return Fail;
        }
        return Ok;
    }
    post([this, fd, handler = std::move(handler)]() mutable {
        subscribe(fd, std::move(handler));
    });
    return Ok;
}

int EventLoopEpoll::unsubscribe(WatchedFd fd){
    if(std::this_thread::get_id() == threadId_){
        handlers_.erase(fd);
        epoll_ctl(epoll_.fd(), EPOLL_CTL_DEL, fd, nullptr);
        return Ok;
    }
    post([this, fd](){
        unsubscribe(fd);
    });
    return Ok;
}

void EventLoopEpoll::post(std::function<void()> op){
    while(!pendingOps_.push(std::move(op))){ // 큐가 가득 차면 busy-wait
        std::this_thread::yield();
    }
    // event loop 깨우기 (stopFd 재사용)
    uint64_t one = 1;
    auto _ = ::write(stopFd_, &one, sizeof(one));
    (void)_;
}

void EventLoopEpoll::drainPendingOps(){
    std::function<void()> op;
    while(pendingOps_.pop(op)){
        op();
    }
}
