#pragma once
#include <cstdint>

struct IpcNewConnection{
    int clientFd;
};

struct IpcDataReceived{
    int clientFd;
};
