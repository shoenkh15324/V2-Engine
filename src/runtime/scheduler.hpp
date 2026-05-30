#pragma once
#include <deque>
#include <unordered_set>
#include "../core/actor/actor.hpp"
#include "../platform/osal/mutex/mutex.hpp"

namespace runtime{
namespace osal = platform::osal;

class Actor;

class Scheduler{
public:
    void notify(Actor* actor);
    Actor* pop();

private:
    osal::Mutex mutex_;
    std::deque<Actor*> readyQueue_;
    std::unordered_set<Actor*> inQueue_;
};

} // namespace runtime
