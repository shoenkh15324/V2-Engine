#pragma once
#include <unordered_set>
#include "core/osal/mutex/mutex.hpp"

namespace core::runtime{

class Scheduler;
class ActorContext;

class Dispatcher{
private:
    using ActorContext = core::runtime::ActorContext;

public:
    explicit Dispatcher(Scheduler* scheduler);
    void registerActor(ActorContext* actor);
    void schedule(ActorContext* actor);

private:
    osal::Mutex mutex_;
    std::unordered_set<ActorContext*> registeredActors_;
    Scheduler* scheduler_;
};

} // namespace core::runtime
