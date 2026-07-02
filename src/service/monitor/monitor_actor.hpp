#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/time/time.hpp"
#include "service/monitor/monitor_data.hpp"
#include "infra/transport/uds/uds_server.hpp"
#include <unordered_set>
#include <vector>
#include <string>
#include <deque>

#if !V2_PLATFORM_WINDOWS

class MonitorActor : public Actor{
public:
    MonitorActor(const std::string& name, uint64_t id, const std::string& socketPath, int backlog, int recvBufferSize, int pollIntervalMs);
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
    void collectSystemResources(SystemResources& resources);

    struct CpuSample{
        uint64_t cpuNs;
        uint64_t wallMs;
    };

    UdsServer server_;
    Time::TimeStamp startTime_;
    std::string socketPath_;
    std::unordered_set<ConnHandle> connections_;
    std::deque<CpuSample> cpuHistory_;
    int backlog_;
    int recvBufferSize_;
    int pollIntervalMs_;
    static constexpr uint64_t kCpuWindowMs = 1000;
};

#endif
