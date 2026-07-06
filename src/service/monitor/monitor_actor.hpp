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
    MonitorActor(const std::string& name, uint64_t id, MonitorConfig config);
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

    UdsServer server_;
    Time::TimeStamp startTime_;
    std::unique_ptr<ISys> sys_;
    std::unique_ptr<IPmu> pmu_;
    MonitorConfig config_;
    std::unordered_set<ConnHandle> connections_;
};

#endif
