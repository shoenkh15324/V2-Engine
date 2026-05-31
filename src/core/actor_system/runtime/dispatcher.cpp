#include "dispatcher.hpp"
#include "core/osal/lock_guard/lock_guard.hpp"

void Dispatcher::schedule(ActorContext* actorCtx){
    LockGuard<Mutex> lock(mutex_);
    if(inQueue_.find(actorCtx) != inQueue_.end()){
        return;
    }
    readyQueue_.push_back(actorCtx);
    inQueue_.insert(actorCtx);
}

ActorContext* Dispatcher::pop(){
    LockGuard<Mutex> lock(mutex_);
    if(readyQueue_.empty()){
        return nullptr;
    }
    ActorContext* actorCtx = readyQueue_.front();
    readyQueue_.pop_front();
    inQueue_.erase(actorCtx);
    return actorCtx;
}
