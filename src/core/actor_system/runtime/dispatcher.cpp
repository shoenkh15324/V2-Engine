#include "dispatcher.hpp"
#include "scheduler.hpp"
#include "core/osal/lock_guard/lock_guard.hpp"

Dispatcher::Dispatcher(Scheduler* scheduler) : scheduler_(scheduler){
}

void Dispatcher::registerActor(ActorContext* actor){
    LockGuard<Mutex> lock(mutex_);
    registeredActors_.insert(actor);
}

void Dispatcher::schedule(ActorContext* actor){
    scheduler_->notify(actor);
}
