#pragma once
#include "core/common/util/platform_config.h"

struct MonitorPoll{
    //
};

struct MonitorNewConnection{
    ConnHandle conn;
};

struct MonitorClientDisconnected{
    ConnHandle conn;
};
