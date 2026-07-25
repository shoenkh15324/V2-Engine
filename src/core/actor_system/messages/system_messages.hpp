#pragma once
#include <cstdint>
#include <string>

struct SignalNotify{
    int signum;
};

struct ActorStateChanged{
    uint64_t actorId;
    std::string actorName;
    uint8_t oldState;
    uint8_t newState;
};
