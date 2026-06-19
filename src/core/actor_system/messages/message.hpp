#pragma once
#include <variant>
#include "timer_messages.hpp"
#include "ipc_messages.hpp"

template<class... Ts>
struct overloaded : Ts...{ 
    using Ts::operator()...;
};

template<class... Ts> 
overloaded(Ts...)->overloaded<Ts...>;

using Message = std::variant<
    // Timer
    TimerExpired,
    // Ipc
    IpcNewConnection,
    IpcDataReceived
>;
