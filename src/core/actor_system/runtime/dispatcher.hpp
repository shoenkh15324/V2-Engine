#pragma once
#include <deque>
#include <unordered_set>
#include "core/osal/mutex/mutex.hpp"

class ActorContext;

class Dispatcher{
public:
    void schedule(ActorContext* actorCtx);
    ActorContext* pop();

private:
    Mutex mutex_;
    std::deque<ActorContext*> readyQueue_;
    std::unordered_set<ActorContext*> inQueue_;
};
