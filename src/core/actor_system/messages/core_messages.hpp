#pragma once
#include <cstdint>
#include <string>
#include "message_traits.hpp"

struct SignalNotify{
    static constexpr MessageId kId = MessageId::SignalNotify;
    int signum;
};

struct ActorEnableRequest{
    static constexpr MessageId kId = MessageId::ActorEnableRequest;
};

struct ActorDisableRequest{
    static constexpr MessageId kId = MessageId::ActorDisableRequest;
};

struct ActorRestartRequest{
    static constexpr MessageId kId = MessageId::ActorRestartRequest;
    std::string reason;
};
