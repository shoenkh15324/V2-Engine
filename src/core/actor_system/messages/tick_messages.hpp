#pragma once
#include "message_traits.hpp"

struct Tick{
    static constexpr MessageId kId = MessageId::Tick;
};
