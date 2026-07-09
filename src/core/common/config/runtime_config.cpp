#include "core/common/config/runtime_config.h"
#include "core/common/config/platform_config.h"
#include <fstream>
#include <nlohmann/json.hpp>

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
        if(j.contains("mailbox_size")) cfg.mailboxSize = j["mailbox_size"];

        // Tick Actor
        if(j.contains("enable_tick")) cfg.enableTick = j["enable_tick"];
        if(j.contains("tick_interval_ms")) cfg.tickIntervalMs = j["tick_interval_ms"];

        // Monitor Actor
        if(j.contains("enable_monitor")) cfg.enableMonitor = j["enable_monitor"];
        if(j.contains("monitor_socket_path")) cfg.monitorSocketPath = j["monitor_socket_path"];
        if(j.contains("monitor_poll_interval_ms")) cfg.monitorPollIntervalMs = j["monitor_poll_interval_ms"];
        if(j.contains("monitor_recv_buffer_size")) cfg.monitorRecvBufferSize = j["monitor_recv_buffer_size"];
        if(j.contains("monitor_backlog")) cfg.monitorBacklog = j["monitor_backlog"];

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
