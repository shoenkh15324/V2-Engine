#pragma once
#include <cstddef>

class ActorRuntime;

class IWorkDispatcher {
public:
    virtual ~IWorkDispatcher() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual bool dispatch(ActorRuntime* actorRuntime) = 0;
    virtual bool redispatch(ActorRuntime* actorRuntime) = 0;
    virtual ActorRuntime* acquire(int workerId) = 0;

    virtual bool isRunning() const = 0;
    virtual void beginDrain() = 0;
    virtual bool isDraining() const = 0;
    virtual size_t pendingWork() const = 0;
    virtual bool finalize(ActorRuntime* actorRuntime) = 0;
    virtual void drainPendedActor(){}
};
