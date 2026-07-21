#pragma once
#include <cstdint>
#include "core/actor_system/messages/message.hpp"

class Actor;
class IScheduler;
class IActorRegistry;
class Dispatcher;

class IActorRuntime{
public:
    virtual ~IActorRuntime() = default;
    virtual void enqueue(Message msg) = 0;
    virtual Actor* actor() const = 0;
    virtual IScheduler* scheduler() const = 0;
    virtual IActorRegistry* actorRegistry() const = 0;
    virtual Dispatcher* dispatcher() const = 0;
    virtual size_t mailboxCount() const = 0;
    virtual size_t mailboxCapacity() const = 0;
};
