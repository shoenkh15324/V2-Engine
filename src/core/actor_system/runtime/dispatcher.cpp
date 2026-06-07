#include "dispatcher.hpp"
#include "core/common/log.hpp"
#include "core/common/return.hpp"

#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <sys/prctl.h>
#endif

Dispatcher::Dispatcher(int workerCount) : workerCount_(workerCount){
}

void Dispatcher::start(){
#ifdef __linux__
    if(epoll_.fd() < 0){ V2_LOG_ERROR("dispatcher needs a valid epoll fd");
        std::abort();
    }
    stopFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(stopFd_ < 0){ V2_LOG_ERROR("eventfd() failed");
        std::abort();
    }
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = stopFd_;
    if(epoll_ctl(epoll_.fd(), EPOLL_CTL_ADD, stopFd_, &ev) < 0){ V2_LOG_ERROR("epoll_ctl ADD stopFd failed");
        std::abort();
    }
#endif
}

Dispatcher::~Dispatcher(){
#ifdef __linux__
    if(stopFd_ >= 0) ::close(stopFd_);
#endif
}

void Dispatcher::dispatch(ActorContext* actorCtx){
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(inQueue_.find(actorCtx) != inQueue_.end()){
            return;
        }
        readyQueue_.push_back(actorCtx);
        inQueue_.insert(actorCtx);
    }
    sema_.post();
}

ActorContext* Dispatcher::acquire(){
    sema_.wait();
    std::lock_guard<std::mutex> lock(mutex_);
    if(readyQueue_.empty()){
        return nullptr;
    }
    ActorContext* ctx = readyQueue_.front();
    readyQueue_.pop_front();
    inQueue_.erase(ctx);
    return ctx;
}

void Dispatcher::stop(){
    running_ = false;
#ifdef __linux__
    uint64_t one = 1;
    ::write(stopFd_, &one, sizeof(one));
#endif
    sema_.post(workerCount_ > 0 ? workerCount_ : 1);
}

void Dispatcher::run(){
#ifdef __linux__
    pthread_setname_np(pthread_self(), "v2-main");
    running_ = true;
    const int maxEvents = 64;
    epoll_event epollEvents[maxEvents];

    while(running_){
        int n = epoll_.wait(epollEvents, maxEvents, 1000);
        if(n < 0){
            if(errno == EINTR) continue;
            break;
        }
        for(int i = 0; i < n; i++){
            if(epollEvents[i].data.fd == stopFd_){
                uint64_t val;
                ssize_t r;
                do{
                    r = ::read(stopFd_, &val, sizeof(val));
                }while(r < 0 && errno == EINTR);
                continue;
            }
            WatchedFd fd = epollEvents[i].data.fd;
            auto it = handlers_.find(fd);
            if(it != handlers_.end()){
                it->second();
            }
        }
    }
#endif
}

int Dispatcher::subscribe(WatchedFd fd, Handler handler){
#ifdef __linux__
    handlers_[fd] = std::move(handler);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if(epoll_ctl(epoll_.fd(), EPOLL_CTL_ADD, fd, &ev) < 0){
        handlers_.erase(fd);
        return Fail;
    }
    return Ok;
#else
    (void)fd; (void)handler;
    return Fail;
#endif
}

int Dispatcher::unsubscribe(WatchedFd fd){
#ifdef __linux__
    handlers_.erase(fd);
    epoll_ctl(epoll_.fd(), EPOLL_CTL_DEL, fd, nullptr);
    return Ok;
#else
    (void)fd;
    return Fail;
#endif
}
