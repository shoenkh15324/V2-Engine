#pragma once
#include <deque>
#include <unordered_set>
#include "core/osal/mutex/mutex.hpp"
#include "core/osal/semaphore/semaphore.hpp"

class ActorContext;

class Dispatcher{
public:
    void schedule(ActorContext* actorCtx);
    ActorContext* pop();
    void wakeup();

private:
    Mutex mutex_;
    Semaphore sema_{0};
    std::deque<ActorContext*> readyQueue_;
    std::unordered_set<ActorContext*> inQueue_;
};
