#pragma once
#include <string>
#include "core/actor_system/messages/message_traits.hpp"
#include "infra/hal/pmu/pmu_data.hpp"

struct PmuDataTick{
    static constexpr MessageId kId = MessageId::PmuDataTick;
};

struct PmuDataSubscribe{
    static constexpr MessageId kId = MessageId::PmuDataSubscribe;
    std::string subscriber;
};

struct PmuDataUnsubscribe{
    static constexpr MessageId kId = MessageId::PmuDataUnsubscribe;
    std::string subscriber;
};

struct PmuDataUpdate{
    static constexpr MessageId kId = MessageId::PmuDataUpdate;
    PmuData data;
};
