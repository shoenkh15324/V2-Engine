#pragma once
#include "core/common/config/platform_config.h"

struct MonitorPoll{
    //
};

struct MonitorNewConnection{
    ConnHandle conn;
};

struct MonitorClientDisconnected{
    ConnHandle conn;
};
