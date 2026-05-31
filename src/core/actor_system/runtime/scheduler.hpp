#pragma once
#include <deque>
#include <unordered_set>
#include "core/osal/mutex/mutex.hpp"

class ActorContext;

class Scheduler{
public:
    void notify(ActorContext* actor);
    ActorContext* pop();

private:
    Mutex mutex_;
    std::deque<ActorContext*> readyQueue_;
    std::unordered_set<ActorContext*> inQueue_;
};
