#pragma once
#include <string>
#include <unordered_set>
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "service/monitor/monitor_messages.hpp"

#if V2_PLATFORM_LINUX
#include "infra/transport/uds/uds_server.hpp"

class MonitorBridgeActor : public Actor {
public:
    MonitorBridgeActor(std::string name, uint64_t id, std::string socketPath, int backlog);
    ~MonitorBridgeActor() override;

    MonitorBridgeActor(const MonitorBridgeActor&) = delete;
    MonitorBridgeActor& operator=(const MonitorBridgeActor&) = delete;
    MonitorBridgeActor(MonitorBridgeActor&&) = delete;
    MonitorBridgeActor& operator=(MonitorBridgeActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;
    void handle(const MonitorNewConnection& m);
    void handle(const MonitorClientDisconnected& m);
    void handle(const MonitorSnapshotUpdate& m);

private:
    void subscribeListener();
    void subscribeClient(ConnHandle conn);
    void unsubscribeAll();

    UdsServer server_;
    int backlog_ = 5;
    std::string socketPath_;
    std::unordered_set<ConnHandle> connections_;
};

#endif // V2_PLATFORM_LINUX
