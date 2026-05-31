#include "scheduler.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/osal/lock_guard/lock_guard.hpp"

namespace core::runtime{

void Scheduler::notify(Actor* actor){
    osal::LockGuard<osal::Mutex> lock(mutex_);
    if(inQueue_.find(actor) != inQueue_.end()){
        return;
    }
    readyQueue_.push_back(actor);
    inQueue_.insert(actor);
}

Scheduler::Actor* Scheduler::pop(){
    osal::LockGuard<osal::Mutex> lock(mutex_);
    if(readyQueue_.empty()){
        return nullptr;
    }
    Actor* actor = readyQueue_.front();
    readyQueue_.pop_front();
    inQueue_.erase(actor);
    return actor;
}

} // namespace core::runtime
