#pragma once
#include <cstdint>
#include <string>
#include "type_id.hpp"

struct SignalNotify{
    static constexpr MessageId kId = MessageId::SignalNotify;
    int signum;
};

struct ActorStateChanged{
    static constexpr MessageId kId = MessageId::ActorStateChanged;
    uint64_t actorId;
    std::string actorName;
    uint8_t oldState;
    uint8_t newState;
};
