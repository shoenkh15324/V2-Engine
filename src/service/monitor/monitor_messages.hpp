#pragma once
#include "core/common/config/platform_config.h"
#include "core/actor_system/messages/message_traits.hpp"
#include "service/monitor/monitor_data.hpp"

struct MonitorNewConnection{
    static constexpr MessageId kId = MessageId::MonitorNewConnection;
    ConnHandle conn;
};

struct MonitorClientDisconnected{
    static constexpr MessageId kId = MessageId::MonitorClientDisconnected;
    ConnHandle conn;
};

struct MonitorSubscribe{
    static constexpr MessageId kId = MessageId::MonitorSubscribe;
    std::string subscriber;
};

struct MonitorUnsubscribe{
    static constexpr MessageId kId = MessageId::MonitorUnsubscribe;
    std::string subscriber;
};

struct MonitorSnapshotUpdate{
    static constexpr MessageId kId = MessageId::MonitorSnapshotUpdate;
    MonitorSnapshot snapshot;
};
