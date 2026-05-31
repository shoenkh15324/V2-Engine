#include "scheduler.hpp"
#include "core/osal/lock_guard/lock_guard.hpp"

void Scheduler::notify(ActorContext* actor){
    LockGuard<Mutex> lock(mutex_);
    if(inQueue_.find(actor) != inQueue_.end()){
        return;
    }
    readyQueue_.push_back(actor);
    inQueue_.insert(actor);
}

ActorContext* Scheduler::pop(){
    LockGuard<Mutex> lock(mutex_);
    if(readyQueue_.empty()){
        return nullptr;
    }
    ActorContext* actor = readyQueue_.front();
    readyQueue_.pop_front();
    inQueue_.erase(actor);
    return actor;
}
