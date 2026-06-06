#include "dispatcher.hpp"
#include "core/common/log.hpp"

#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#endif

Dispatcher::Dispatcher()
#ifdef __linux__
    : eventFd_(eventfd(0, EFD_SEMAPHORE | EFD_CLOEXEC))
#endif
{
#ifdef __linux__
    if(eventFd_ < 0){ V2_LOG_ERROR("eventfd() failed");
        std::abort();
    }
#endif
}

Dispatcher::~Dispatcher(){
#ifdef __linux__
    if(eventFd_ >= 0){
        ::close(eventFd_);
    }
#endif
}

void Dispatcher::schedule(ActorContext* actorCtx){
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(inQueue_.find(actorCtx) != inQueue_.end()){
            return;
        }
        readyQueue_.push_back(actorCtx);
        inQueue_.insert(actorCtx);
    }
#ifdef __linux__
    uint64_t one = 1;
    ::write(eventFd_, &one, sizeof(one));
#else
    sema_.post();
#endif
}

ActorContext* Dispatcher::pop(){
#ifdef __linux__
    uint64_t val;
    ssize_t n;
    do{
        n = ::read(eventFd_, &val, sizeof(val));
    } while(n < 0 && errno == EINTR);
#else
    sema_.wait();
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    if(readyQueue_.empty()){
        return nullptr;
    }
    ActorContext* ctx = readyQueue_.front();
    readyQueue_.pop_front();
    inQueue_.erase(ctx);
    return ctx;
}

ActorContext* Dispatcher::tryPop(){
    std::lock_guard<std::mutex> lock(mutex_);
    if(readyQueue_.empty()){
        return nullptr;
    }
    ActorContext* ctx = readyQueue_.front();
    readyQueue_.pop_front();
    inQueue_.erase(ctx);
    return ctx;
}

void Dispatcher::wakeup(){
#ifdef __linux__
    uint64_t one = 1;
    ::write(eventFd_, &one, sizeof(one));
#else
    sema_.post();
#endif
}
