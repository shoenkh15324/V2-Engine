#include "scheduler.hpp"
#include "../core/actor/actor.hpp"
#include "../platform/osal/lock_guard/lock_guard.hpp"

namespace runtime{
namespace osal = platform::osal;

void Scheduler::notify(Actor* actor){
    osal::LockGuard<osal::Mutex> lock(mutex_);
    if(inQueue_.find(actor) != inQueue_.end()){
        return;
    }
    readyQueue_.push_back(actor);
    inQueue_.insert(actor);
}

Actor* Scheduler::pop(){
    osal::LockGuard<osal::Mutex> lock(mutex_);
    if(readyQueue_.empty()){
        return nullptr;
    }
    Actor* actor = readyQueue_.front();
    readyQueue_.pop_front();
    inQueue_.erase(actor);
    return actor;
}

} // namespace runtime
