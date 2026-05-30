#include "dispatcher.hpp"
#include "scheduler.hpp"
#include "../platform/osal/lock_guard/lock_guard.hpp"

namespace runtime{
namespace osal = platform::osal;

Dispatcher::Dispatcher(Scheduler* scheduler) : scheduler_(scheduler){
}

void Dispatcher::registerActor(Actor* actor){
    osal::LockGuard<osal::Mutex> lock(mutex_);
    registeredActors_.insert(actor);
}

void Dispatcher::notify(Actor* actor){
    scheduler_->notify(actor);
}

} // namespace runtime
