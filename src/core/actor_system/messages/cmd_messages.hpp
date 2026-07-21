#pragma once
#include "core/common/config/platform_config.h"
#include <string>

struct CmdRequest{
    ConnHandle conn;
    std::string cmd;
};

struct CmdResponse{
    ConnHandle conn;
    std::string result;
};

struct ActorEnableRequest{};

struct ActorDisableRequest{};
