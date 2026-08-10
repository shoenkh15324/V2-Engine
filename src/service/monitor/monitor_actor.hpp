#pragma once
#include <memory>
#include <vector>
#include <string>
#include <unordered_set>
#include "core/actor_system/actor/actor.hpp"
#include "core/common/time/time.hpp"
#include "core/common/config/platform_config.h"
#include "infra/transport/uds/uds_server.hpp"
#include "service/monitor/monitor_data.hpp"

class MonitorActor : public Actor{
public:
    MonitorActor(std::string name, uint64_t id);
    ~MonitorActor() override = default;

    MonitorActor(const MonitorActor&) = delete;
    MonitorActor& operator=(const MonitorActor&) = delete;
    MonitorActor(MonitorActor&&) = delete;
    MonitorActor& operator=(MonitorActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    void collectActorInfo(std::vector<ActorInfo>& actors);
    void subscribeToData();
    void unsubscribeFromData();
    void tryPublish();

    PmuData pmuDataCache_;
    SystemResources sysResCache_;
    bool dataSubscribed_ = false;
    std::unordered_set<std::string> subscribers_;
};
