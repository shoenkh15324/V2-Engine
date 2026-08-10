#pragma once
#include <cstdint>

enum class MessageId : uint32_t {
    // core
    SignalNotify = 1,
    // Actor
    ActorEnableRequest,
    ActorDisableRequest,
    ActorRestartRequest,
    // Tick
    Tick,
    // Cmd
    CmdRequest,
    CmdResponse,
    // Ipc
    IpcNewConnection,
    IpcDataReceived,
    // Monitor
    MonitorNewConnection,
    MonitorClientDisconnected,
    MonitorSubscribe,
    MonitorUnsubscribe,
    MonitorSnapshotUpdate,
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
    WifiAutoReconnectRequest,
    // Pmu
    PmuDataTick,
    PmuDataSubscribe,
    PmuDataUnsubscribe,
    PmuDataUpdate,
    // System
    SysDataTick,
    SysDataSubscribe,
    SysDataUnsubscribe,
    SysDataUpdate,
};
