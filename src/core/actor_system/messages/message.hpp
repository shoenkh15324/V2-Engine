#pragma once
#include <variant>
#include "tick_messages.hpp"
#include "ipc_messages.hpp"
#include "monitor_messages.hpp"
#include "dbus_messages.hpp"
#include "device_manager_messages.hpp"
#include "cmd_messages.hpp"
#include "network_manager/network_manager_messages.hpp"
#include "network_manager/wifi_messages.hpp"

template<class... Ts>
struct overloaded : Ts...{ 
    using Ts::operator()...;
};

template<class... Ts> 
overloaded(Ts...)->overloaded<Ts...>;

using Message = std::variant<
    // Tick
    Tick,
    // Ipc
    IpcNewConnection,
    IpcDataReceived,
    // Monitor
    MonitorPoll,
    MonitorNewConnection,
    MonitorClientDisconnected,
    // Dbus
    DbusRegisterMethod,
    DbusUnregisterMethod,
    DbusRegisterResult,
    DbusIncomingMethodCall,
    DbusMethodCallResult,
    DbusProxyCallRequest,
    DbusProxyCallResult,
    DbusSubscribeSignal,
    DbusSignalEvent,
    // Device Manager
    DeviceRegister,
    DeviceUnregister,
    DeviceEnumerate,
    DeviceList,
    // Cmd
    CmdRequest,
    CmdResponse,
    // Network Manager
    NmStatusRequest,
    // Wifi
    WifiScanRequest,
    WifiScanResult,
    WifiConnectRequest,
    WifiConnectResult,
    WifiDisconnectRequest,
    WifiDisconnectResult,
    WifiStatusResult,
    WifiAutoReconnectRequest
>;
