#pragma once
#include <string>
#include "core/actor_system/messages/message_traits.hpp"
#include "infra/hal/sys/system_data.hpp"

struct SysDataTick{
    static constexpr MessageId kId = MessageId::SysDataTick; 
};

struct SysDataSubscribe{
    static constexpr MessageId kId = MessageId::SysDataSubscribe;
    std::string subscriber;
};

struct SysDataUnsubscribe{
    static constexpr MessageId kId = MessageId::SysDataUnsubscribe;
    std::string subscriber;
};

struct SysDataUpdate{
    static constexpr MessageId kId = MessageId::SysDataUpdate;
    SystemResources data;
};
