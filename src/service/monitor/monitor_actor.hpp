#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/time/time.hpp"
#include "service/monitor/monitor_data.hpp"
#include "infra/hal/sys/i_sys.hpp"
#include "infra/hal/pmu/i_pmu.hpp"
#include "infra/transport/uds/uds_server.hpp"
#include <memory>
#include <unordered_set>
#include <vector>
#include <string>

struct MonitorConfig{
    std::string socketPath;
    int backlog;
    int pollIntervalMs;
};

#if V2_PLATFORM_LINUX

class MonitorActor : public Actor{
public:
    MonitorActor(std::string name, uint64_t id, MonitorConfig config, ISys* sys, IPmu* pmu);
    ~MonitorActor() override;

    MonitorActor(const MonitorActor&) = delete;
    MonitorActor& operator=(const MonitorActor&) = delete;
    MonitorActor(MonitorActor&&) = delete;
    MonitorActor& operator=(MonitorActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    void subscribeListener();
    void subscribeClient(ConnHandle conn);
    void unsubscribeAll();
    void collectActorInfo(std::vector<ActorInfo>& actors);
    void collectSystemResources(SystemResources& resources);
    void prepareSnapshot(MonitorSnapshot& snap);
    void broadcastSnapshot(const MonitorSnapshot& snap);

    ISys* sys_ = nullptr;
    IPmu* pmu_ = nullptr;
    UdsServer server_;
    MonitorConfig config_;
    Time::TimeStamp startTime_;
    std::unordered_set<ConnHandle> connections_;
};

#endif
