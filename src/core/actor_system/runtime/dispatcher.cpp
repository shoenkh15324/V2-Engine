#include "dispatcher.hpp"

void Dispatcher::schedule(ActorContext* actorCtx){
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

ActorContext* Dispatcher::pop(){
    sema_.wait();
    std::lock_guard<std::mutex> lock(mutex_);
    if(readyQueue_.empty()){
        return nullptr;
    }
    ActorContext* actorCtx = readyQueue_.front();
    readyQueue_.pop_front();
    inQueue_.erase(actorCtx);
    return actorCtx;
}

void Dispatcher::wakeup(){
    sema_.post();
}
