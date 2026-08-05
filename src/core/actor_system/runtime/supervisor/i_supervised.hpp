#pragma once
#include <string>
#include "core/actor_system/messages/message.hpp"

/* 
    Supervisor가 의존하는 최소 인터페이스.
    ActorRuntime이 구현하며, 테스트에서 Mock으로 대체할 수 있다.
*/
class ISupervised{
public:
    virtual ~ISupervised() = default;

    // OneForOne 경로. maxRestarts 한계 안에서만 원자적으로 재시작.
    // 반환값: 실제 재시작이 수행되었으면 true.
    virtual bool tryRestart(const std::string& reason, int maxRestarts) = 0;

    // None 정책(영구 중단) 시 호출. close 후 더 이상 디스패치하지 않는다.
    virtual void shutdown() = 0;

    // 메일박스에서 메시지를 하나 꺼낸다. 비어 있으면 false. (dead letter drain용)
    virtual bool popMessage(Message& msg) = 0;

    // 누적 재시작 횟수 (OneForOne 실패 루프 방지용 한계 판정에 사용).
    // OneForAll 브로드캐스트 재시작은 증가시키지 않는다.
    virtual int restartCount() const = 0;
    virtual uint64_t actorId() const = 0;
    virtual const std::string& actorName() const = 0;
};
