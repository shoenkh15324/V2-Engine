#pragma once
#include "core/common/util/platform_config.h"

struct IpcNewConnection{
    ConnHandle conn;
};

struct IpcDataReceived{
    ConnHandle conn;
};
