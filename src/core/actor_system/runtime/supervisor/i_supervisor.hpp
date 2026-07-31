#pragma once
#include <string>
#include "core/actor_system/messages/message.hpp"

class ISupervised;

class ISupervisor{
public:
    virtual ~ISupervisor() = default;

    // ActorRuntime::run()이 handle() 예외를 잡았을 때 호출된다.
    // 호출 스레드: 크래시가 발생한 워커 스레드. 여러 워커에서 동시 호출 가능.
    virtual void onActorFailed(ISupervised* runtime, Message failedMsg, const std::string& reason) = 0;
};
