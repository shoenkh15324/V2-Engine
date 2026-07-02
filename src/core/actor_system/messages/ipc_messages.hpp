#pragma once
#include "core/common/config/platform_config.h"

struct IpcNewConnection{
    ConnHandle conn;
};

struct IpcDataReceived{
    ConnHandle conn;
};
