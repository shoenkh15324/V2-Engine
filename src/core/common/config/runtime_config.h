#pragma once
#include <string>
#include <cstdint>
#include "core/common/config/platform_config.h"

struct RuntimeConfig{
    // Engine
#ifdef V2_DEFAULT_LOG_LEVEL
    int logLevel = V2_DEFAULT_LOG_LEVEL;
#else
    int logLevel = 4; // Error
#endif
    int workerCount = 4;
    int workerMaxBatch = 32;
    int mainLoopSleepMs = 1000;
    int busyStealIntervalUs = 200;
    int idleStealIntervalUs = 2000;

    // Mailbox
    int defaultMailboxSize = 512;

    // Dispatcher
    int dispatcherQueueCapacity = 1024;
    int dispatcherHighWatermark = 0; // 0이면 kQueueCapacity * 7 / 10 자동 계산

    // Supervisor
    int supervisorMaxRestarts = 5;
    int supervisorDefaultStrategy = 0; // 0=OneForOne, 1=OneForAll, 2=None
    int deadLetterQueueCapacity = 128;

    // Logging
    int logFlushBufferSize = 512;

    // Memory
    int memorySlabSize = 4096;
    int memoryMaxPoolAllocSize = 2048;
    int memoryMaxPools = 16;
    int memoryTlsBatchSize = 64;

    // Message
    int messageInlineBufferSize = 64;

    // Tick
    bool enableTick = false;
    int tickIntervalMs = 100;

    // Monitor
    bool enableMonitor = false;
    std::string monitorSocketPath = "/tmp/v2_monitor.sock";
    int monitorPollIntervalMs = 500;
    int monitorRecvBufferSize = 4096;
    int monitorBacklog = 5;

    // Device Manager
    bool enableDeviceManager = false;

    // Metrics
    bool enableMetrics = false;

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

    // Network Manager
    bool enableNetworkManager = false;
#endif

    static RuntimeConfig loadFromFile(const std::string& path);
};
