#pragma once
#include <variant>
#include "timer_messages.hpp"

using Message = std::variant<
    TimerExpired
>;
