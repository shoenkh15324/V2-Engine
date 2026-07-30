#pragma once
#include "core/actor_system/messages/message_traits.hpp"

struct NmStatusRequest{
    static constexpr MessageId kId = MessageId::NmStatusRequest;
};
