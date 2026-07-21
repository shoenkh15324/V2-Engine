#pragma once
#include <cstdint>
#include "core/actor_system/messages/message.hpp"

class IActorRuntime;

class IScheduler{
public:
    virtual ~IScheduler() = default;
    virtual int addTimer(IActorRuntime* target, Message message, uint64_t delayMs, bool repeating) = 0;
    virtual void cancel(int timerId) = 0;
};
