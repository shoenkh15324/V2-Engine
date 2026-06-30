#pragma once
#include <string>
#include <cstdint>
#include "core/common/util/platform_config.h"

struct RuntimeConfig{
    // Engine
    int logLevel = 3;
    int workerCount = 1;
    int workerMaxBatch = 32;
    int mainLoopSleepMs = 1000;
    int mailboxSize = 512;

    // Tick
    bool enableTick = false;
    int tickIntervalMs = 100;

    // Monitor
    bool enableMonitor = false;
    std::string monitorSocketPath = "/tmp/v2_monitor.sock";
    int monitorPollIntervalMs = 100;
    int monitorRecvBufferSize = 4096;
    int monitorBacklog = 5;

#if V2_PLATFORM_LINUX
    // Epoll
    int epollMaxEvents = 64;
    int epollWaitTimeoutMs = 1000;

    // IPC
    bool enableIpcServer = false;
    std::string ipcSocketPath = "/tmp/v2_ipc.sock";
    int ipcRecvBufferSize = 4096;
    int udsBacklog = 5;

    // Dbus
    bool enableDbus = false;
    std::string dbusBusName = "com.v2.engine";
    std::string dbusObjectPath = "/com/v2/engine";
    std::string dbusInterfaceName = "com.v2.engine";
#endif

    static RuntimeConfig loadFromFile(const std::string& path);
};
