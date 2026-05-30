#pragma once
#include <deque>
#include <unordered_set>
#include "../osal/mutex/mutex.hpp"

namespace core::actor{
class Actor;
} // namespace core::actor

namespace core::runtime{

class Scheduler{
public:
    using Actor = core::actor::Actor;

    void notify(Actor* actor);
    Actor* pop();

private:
    osal::Mutex mutex_;
    std::deque<Actor*> readyQueue_;
    std::unordered_set<Actor*> inQueue_;
};

} // namespace core::runtime
