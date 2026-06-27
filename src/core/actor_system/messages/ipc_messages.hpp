#pragma once
#include "core/common/platform_config.h"

struct IpcNewConnection{
    ConnHandle conn;
};

struct IpcDataReceived{
    ConnHandle conn;
};
