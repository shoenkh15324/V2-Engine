#pragma once
#include "core/common/config/platform_config.h"
#include "message_traits.hpp"

struct IpcNewConnection{
    static constexpr MessageId kId = MessageId::IpcNewConnection;
    ConnHandle conn;
};

struct IpcDataReceived{
    static constexpr MessageId kId = MessageId::IpcDataReceived;
    ConnHandle conn;
};
