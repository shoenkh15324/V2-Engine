#include "dispatcher.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/debug.hpp"
#include "core/common/util/return.hpp"
#include "core/perf/metrics/metrics.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdlib>

#if V2_PLATFORM_LINUX
    #include <sys/eventfd.h>
    #include <unistd.h>
    #include <sys/prctl.h>
#endif

Dispatcher::Dispatcher(int workerCount, int epollMaxEvents, int epollWaitTimeoutMs)
    : workerCount_(workerCount), epollMaxEvents_(epollMaxEvents), epollWaitTimeoutMs_(epollWaitTimeoutMs)
#if V2_PLATFORM_LINUX
    , epollEvents_(epollMaxEvents)
#endif
{
    for(int i = 0; i < workerCount; i++){
        queues_.push_back(std::make_unique<LockFreeMpscQueue<ActorContext*>>(1024));
        semas_.push_back(std::make_unique<std::counting_semaphore<>>(0));
    }
}

void Dispatcher::start(){
#if V2_PLATFORM_LINUX
    V2_ASSERT(epoll_.fd() >= 0, "dispatcher needs a valid epoll fd");
    stopFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    V2_ASSERT(stopFd_ >= 0, "eventfd() failed");
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = stopFd_;
    int result = epoll_ctl(epoll_.fd(), EPOLL_CTL_ADD, stopFd_, &ev);
    V2_ASSERT(result >= 0, "epoll_ctl ADD stopFd failed");
#endif
}

Dispatcher::~Dispatcher(){
#if V2_PLATFORM_LINUX
    if(stopFd_ >= 0) ::close(stopFd_);
#endif
}

void Dispatcher::dispatch(ActorContext* actorCtx){
    uint64_t actorId = actorCtx->actor()->id();
    int workerId = static_cast<int>(actorId % workerCount_);
    queues_[workerId]->push(std::move(actorCtx));
    Metrics::recordDispatch(false, queues_[workerId]->count());
    semas_[workerId]->release();
}

ActorContext* Dispatcher::acquire(int workerId){
   semas_[workerId]->acquire();
    ActorContext* ctx = nullptr;
    queues_[workerId]->pop(ctx);
    if(ctx) Metrics::recordAcquire();
    return ctx;
}

void Dispatcher::stop(){
    running_ = false;
#if V2_PLATFORM_LINUX
    uint64_t one = 1;
    auto _ = ::write(stopFd_, &one, sizeof(one));
    (void)_;
#endif
    for(int i = 0; i < workerCount_; i++){
        semas_[i]->release();
    }
}

void Dispatcher::run(){
#if V2_PLATFORM_LINUX
    pthread_setname_np(pthread_self(), "v2-main");
    running_ = true;
    while(running_){
        int n = epoll_.wait(epollEvents_.data(), epollMaxEvents_, epollWaitTimeoutMs_);
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
#endif
}

int Dispatcher::subscribe(WatchedFd fd, Handler handler){
#if V2_PLATFORM_LINUX
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
#if V2_PLATFORM_LINUX
    handlers_.erase(fd);
    epoll_ctl(epoll_.fd(), EPOLL_CTL_DEL, fd, nullptr);
    return Ok;
#else
    (void)fd;
    return Fail;
#endif
}
