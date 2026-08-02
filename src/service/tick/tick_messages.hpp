#pragma once
#include "core/actor_system/messages/message_traits.hpp"

struct Tick{
    static constexpr MessageId kId = MessageId::Tick;
};
