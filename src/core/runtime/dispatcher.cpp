#include "dispatcher.hpp"
#include "scheduler.hpp"
#include "../osal/lock_guard/lock_guard.hpp"

namespace core::runtime{

Dispatcher::Dispatcher(Scheduler* scheduler) : scheduler_(scheduler){
}

void Dispatcher::registerActor(Actor* actor){
    osal::LockGuard<osal::Mutex> lock(mutex_);
    registeredActors_.insert(actor);
}

void Dispatcher::notify(Actor* actor){
    scheduler_->notify(actor);
}

} // namespace core::runtime
