#pragma once
#include <cstdint>
#include "core/actor_system/messages/message.hpp"

class IActorRuntime;

class IScheduler{
public:
    virtual ~IScheduler() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual int addTimer(IActorRuntime* target, Message msg, uint64_t delayMs, bool repeating) = 0;
    virtual void cancel(int id) = 0;
};
