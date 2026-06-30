#pragma once
#include <variant>
#include "tick_messages.hpp"
#include "ipc_messages.hpp"
#include "monitor_messages.hpp"
#include "dbus_messages.hpp"

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
    MonitorClientDisconnected
>;
