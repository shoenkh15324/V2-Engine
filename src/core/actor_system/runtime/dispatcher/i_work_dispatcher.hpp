#pragma once

class ActorRuntime;

class IWorkDispatcher{
public:
    virtual ~IWorkDispatcher() = default;
    virtual void dispatch(ActorRuntime* actorRuntime) = 0;
    virtual ActorRuntime* acquire(int workerId) = 0;
    virtual bool isRunning() const = 0;
};
