#pragma once
#include "core/actor_system/messages/type_id.hpp"

struct NmStatusRequest{
    static constexpr MessageId kId = MessageId::NmStatusRequest;
};
