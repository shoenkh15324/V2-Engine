#pragma once
#include <cstdint>
#include "core/actor_system/messages/message.hpp"

class Actor;
class IScheduler;
class IActorRegistry;
class IWorkDispatcher;
class IEventLoop;

class IActorRuntime{
public:
    virtual ~IActorRuntime() = default;
    virtual void enqueue(Message msg) = 0;
    virtual Actor* actor() const = 0;
    virtual IActorRegistry* actorRegistry() const = 0;
    virtual IEventLoop* eventLoop() const = 0;
    virtual size_t mailboxCount() const = 0;
    virtual size_t mailboxCapacity() const = 0;

    virtual int addTimer(Actor* target, Message msg, uint64_t delayMs, bool repeating) = 0;
    virtual void cancelTimer(int timerId) = 0;
    virtual void cancelAllTimers() = 0;
    virtual size_t timerCount() const = 0;
};
