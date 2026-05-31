#pragma once
#include <deque>
#include <unordered_set>
#include <mutex>
#include "core/common/semaphore.hpp"

class ActorContext;

class Dispatcher{
public:
    void schedule(ActorContext* actorCtx);
    ActorContext* pop();
    void wakeup();

private:
    std::mutex mutex_;
    Semaphore sema_{0};
    std::deque<ActorContext*> readyQueue_;
    std::unordered_set<ActorContext*> inQueue_;
};
