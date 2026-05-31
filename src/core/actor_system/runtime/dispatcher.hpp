#pragma once
#include <unordered_set>
#include "core/osal/mutex/mutex.hpp"

class Scheduler;
class ActorContext;

class Dispatcher{
public:
    explicit Dispatcher(Scheduler* scheduler);
    void registerActor(ActorContext* actor);
    void schedule(ActorContext* actor);

private:
    Mutex mutex_;
    std::unordered_set<ActorContext*> registeredActors_;
    Scheduler* scheduler_;
};
