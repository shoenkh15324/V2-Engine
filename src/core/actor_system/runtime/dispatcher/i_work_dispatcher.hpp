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
// 배치 종료 후 토큰 정산: 반납 → 메일박스 재확인 → 이양(redispatch) 또는 회수.
    // 재디스패치되면 true.
    virtual bool settleToken(ActorRuntime* actorRuntime) = 0;
    virtual void drainPendedActor(){}
};
