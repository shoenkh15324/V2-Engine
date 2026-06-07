#include "worker.hpp"
#include "dispatcher.hpp"
#include "core/actor_system/actor/actor_context.hpp"

#ifdef __linux__
    #include <sys/epoll.h>
#endif

Worker::Worker(Dispatcher* dispatcher, int maxBatch) : dispatcher_(dispatcher), maxBatch_(maxBatch){
    //
}

Worker::~Worker(){
    stop();
}

void Worker::start(){
    running_ = true;
    thread_ = std::thread([this]{ runLoop(); });
}

void Worker::stop(){
    running_ = false;
    dispatcher_->wakeup();
    if(thread_.joinable()){
        thread_.join();
    }
}

void Worker::runLoop(){
#ifdef __linux__
    const int maxEvents = 16;
    epoll_event events[maxEvents];
    while(running_){
        int n = epoll_wait(dispatcher_->epollFd(), events, maxEvents, -1);
        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            break;
        }
        for(int i = 0; i < n; i++){
            ActorContext* actorCtx = dispatcher_->tryPop();
            if(actorCtx){
                actorCtx->run(maxBatch_);
            }
        }
    }
#else
    while(running_){
        ActorContext* actorCtx = dispatcher_->pop();
        if(!actorCtx){
            continue;
        }
        actorCtx->run(maxBatch_);
    }
#endif
}
