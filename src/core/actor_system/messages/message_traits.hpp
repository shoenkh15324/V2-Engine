#pragma once
#include <cstdint>

enum class MessageId : uint32_t {
// System
    SignalNotify = 1,
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
    // Lifecycle
    ActorEnableRequest,
    ActorDisableRequest,
    ActorRestartRequest,
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
};
