#pragma once
#include <deque>
#include <unordered_set>
#include <mutex>

#ifndef __linux__
#include "core/common/semaphore.hpp"
#endif

class ActorContext;

class Dispatcher{
public:
    Dispatcher();
    ~Dispatcher();
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;
    Dispatcher(Dispatcher&&) = delete;
    Dispatcher& operator=(Dispatcher&&) = delete;

    void schedule(ActorContext* actorCtx);
    ActorContext* pop();
    ActorContext* tryPop();
    void wakeup();

#ifdef __linux__
    int eventFd() const { return eventFd_; }
#endif

private:
    std::mutex mutex_;

#ifdef __linux__
    int eventFd_;
#else
    Semaphore sema_{0};
#endif

    std::deque<ActorContext*> readyQueue_;
    std::unordered_set<ActorContext*> inQueue_;
};
