#pragma once
#include "core/common/config.h"

struct IpcNewConnection{
    ConnHandle conn;
};

struct IpcDataReceived{
    ConnHandle conn;
};
