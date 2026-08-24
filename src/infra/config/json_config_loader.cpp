#include <fstream>
#include <nlohmann/json.hpp>
#include "core/common/config/runtime_config.h"
#include "core/common/config/platform_config.h"

RuntimeConfig RuntimeConfig::loadFromFile(const std::string& path){
    RuntimeConfig cfg;
    std::ifstream file(path);
    if(!file.is_open()) return cfg;

    try{
        nlohmann::json j;
        file >> j;

        // Engine
        if(j.contains("log_level")) cfg.logLevel = j["log_level"];
        if(j.contains("worker_count")) cfg.workerCount = j["worker_count"];
        if(j.contains("worker_max_batch")) cfg.workerMaxBatch = j["worker_max_batch"];
        if(j.contains("mainloop_sleep_ms")) cfg.mainLoopSleepMs = j["mainloop_sleep_ms"];
        if(j.contains("busy_steal_interval_us")) cfg.busyStealIntervalUs = j["busy_steal_interval_us"];
        if(j.contains("idle_steal_interval_us")) cfg.idleStealIntervalUs = j["idle_steal_interval_us"];
        if(j.contains("park_spin_ns")) cfg.parkSpinNs = j["park_spin_ns"];
        if(j.contains("token_grace_ns")) cfg.tokenGraceNs = j["token_grace_ns"];

        // Mailbox
        if(j.contains("default_mailbox_size")) cfg.defaultMailboxSize = j["default_mailbox_size"];

        // Dispatcher
        if(j.contains("dispatcher_queue_capacity")) cfg.dispatcherQueueCapacity = j["dispatcher_queue_capacity"];
        if(j.contains("dispatcher_high_watermark")) cfg.dispatcherHighWatermark = j["dispatcher_high_watermark"];

        // Supervisor
        if(j.contains("supervisor_max_restarts")) cfg.supervisorMaxRestarts = j["supervisor_max_restarts"];
        if(j.contains("supervisor_default_strategy")) cfg.supervisorDefaultStrategy = j["supervisor_default_strategy"];
        if(j.contains("dead_letter_queue_capacity")) cfg.deadLetterQueueCapacity = j["dead_letter_queue_capacity"];

        // Logging
        if(j.contains("log_flush_buffer_size")) cfg.logFlushBufferSize = j["log_flush_buffer_size"];

        // Memory
        if(j.contains("memory_slab_size")) cfg.memorySlabSize = j["memory_slab_size"];
        if(j.contains("memory_max_pool_alloc_size")) cfg.memoryMaxPoolAllocSize = j["memory_max_pool_alloc_size"];
        if(j.contains("memory_max_pools")) cfg.memoryMaxPools = j["memory_max_pools"];
        if(j.contains("memory_tls_batch_size")) cfg.memoryTlsBatchSize = j["memory_tls_batch_size"];

        // Message
        if(j.contains("message_inline_buffer_size")) cfg.messageInlineBufferSize = j["message_inline_buffer_size"];

        // Tick Actor
        if(j.contains("enable_tick")) cfg.enableTick = j["enable_tick"];
        if(j.contains("tick_interval_ms")) cfg.tickIntervalMs = j["tick_interval_ms"];

        // Monitor Actor
        if(j.contains("enable_monitor")) cfg.enableMonitor = j["enable_monitor"];
        if(j.contains("monitor_socket_path")) cfg.monitorSocketPath = j["monitor_socket_path"];
        if(j.contains("monitor_poll_interval_ms")) cfg.monitorPollIntervalMs = j["monitor_poll_interval_ms"];
        if(j.contains("monitor_recv_buffer_size")) cfg.monitorRecvBufferSize = j["monitor_recv_buffer_size"];
        if(j.contains("monitor_backlog")) cfg.monitorBacklog = j["monitor_backlog"];

        // Device Manager
        if(j.contains("enable_device_manager")) cfg.enableDeviceManager = j["enable_device_manager"];

        // Metrics
        if(j.contains("enable_metrics")) cfg.enableMetrics = j["enable_metrics"];

#if V2_PLATFORM_LINUX

        // Epoll
        if(j.contains("epoll_max_events")) cfg.epollMaxEvents = j["epoll_max_events"];
        if(j.contains("epoll_wait_timeout_ms")) cfg.epollWaitTimeoutMs = j["epoll_wait_timeout_ms"];

        // Ipc Server Actor
        if(j.contains("enable_ipc_server")) cfg.enableIpcServer = j["enable_ipc_server"];
        if(j.contains("ipc_socket_path")) cfg.ipcSocketPath = j["ipc_socket_path"];
        if(j.contains("ipc_recv_buffer_size")) cfg.ipcRecvBufferSize = j["ipc_recv_buffer_size"];
        if(j.contains("uds_backlog")) cfg.udsBacklog = j["uds_backlog"];

        // Dbus Actor
        if(j.contains("enable_dbus")) cfg.enableDbus = j["enable_dbus"];
        if(j.contains("dbus_bus_name")) cfg.dbusBusName = j["dbus_bus_name"];
        if(j.contains("dbus_object_path")) cfg.dbusObjectPath = j["dbus_object_path"];
        if (j.contains("dbus_interface_name")) cfg.dbusInterfaceName = j["dbus_interface_name"];

        // Network Manager
        if(j.contains("enable_network_manager")) cfg.enableNetworkManager = j["enable_network_manager"];

#endif
    }catch(...){}
    return cfg;
}
