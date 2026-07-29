#pragma once
#include "core/common/config/platform_config.h"
#include "type_id.hpp"

struct MonitorPoll{
    static constexpr MessageId kId = MessageId::MonitorPoll;
};

struct MonitorNewConnection{
    static constexpr MessageId kId = MessageId::MonitorNewConnection;
    ConnHandle conn;
};

struct MonitorClientDisconnected{
    static constexpr MessageId kId = MessageId::MonitorClientDisconnected;
    ConnHandle conn;
};
