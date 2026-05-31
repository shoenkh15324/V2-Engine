#pragma once
#include <unordered_set>
#include "core/osal/mutex/mutex.hpp"

namespace core::actor{
class Actor;
} // namespace core::actor

namespace core::runtime{

class Scheduler;

class Dispatcher{
private:
    using Actor = core::actor::Actor;

public:
    explicit Dispatcher(Scheduler* scheduler);
    void registerActor(Actor* actor);
    void notify(Actor* actor);

private:
    osal::Mutex mutex_;
    std::unordered_set<Actor*> registeredActors_;
    Scheduler* scheduler_;
};

} // namespace core::runtime
