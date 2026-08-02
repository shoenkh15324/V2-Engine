#pragma once
#include "core/common/config/platform_config.h"
#include <string>
#include "core/actor_system/messages/message_traits.hpp"

struct CmdRequest{
    static constexpr MessageId kId = MessageId::CmdRequest;
    ConnHandle conn;
    std::string cmd;
};

struct CmdResponse{
    static constexpr MessageId kId = MessageId::CmdResponse;
    ConnHandle conn;
    std::string result;
};
