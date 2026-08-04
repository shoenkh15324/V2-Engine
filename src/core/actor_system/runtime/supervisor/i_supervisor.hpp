#pragma once
#include <string>
#include <functional>
#include "core/actor_system/messages/message.hpp"

class ISupervised;

class ISupervisor{
public:
    virtual ~ISupervisor() = default;

    virtual void onActorFailed(ISupervised* runtime, Message failedMsg, const std::string& reason) = 0;
    virtual void setRestartAll(std::function<int()> restartAll) = 0;
};
