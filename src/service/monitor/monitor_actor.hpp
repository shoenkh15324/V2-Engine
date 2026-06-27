#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/util/platform_config.h"
#include "core/common/time/time.hpp"
#include "infra/transport/uds/uds_server.hpp"
#include <unordered_set>
#include <vector>
#include <string>

struct MonitorSnapshot{
    struct ActorInfo{
        std::string name;
        uint64_t id;
        size_t mailboxCount;
        size_t mailboxCapacity;
    };
    struct SystemResources{
        uint64_t memoryRssKb = 0;
        uint64_t memoryTotalKb = 0;
        float cpuPercent = 0.0;
    };
    uint64_t timestampMs;
    std::vector<ActorInfo> actors;
    SystemResources resources;
    int clientCount;
};

#if !V2_PLATFORM_WINDOWS

class MonitorActor : public Actor{
public:
    MonitorActor(const std::string& name, uint64_t id, const std::string& socketPath, int backlog, int recvBufferSize, int pollIntervalMs);
    ~MonitorActor() override;

    MonitorActor(const MonitorActor&) = delete;
    MonitorActor& operator=(const MonitorActor&) = delete;
    MonitorActor(MonitorActor&&) = delete;
    MonitorActor& operator=(MonitorActor&&) = delete;

    void onStart() override;
    void handle(const Message& msg) override;

private:
    void subscribeListener();
    void subscribeClient(ConnHandle conn);
    void unsubscribeAll();
    void collectSystemResources(MonitorSnapshot::SystemResources& resources);
    std::string serializeSnapshot(const MonitorSnapshot& snap);

    UdsServer server_;
    std::string socketPath_;
    int backlog_;
    int recvBufferSize_;
    int pollIntervalMs_;
    std::unordered_set<ConnHandle> connections_;
    Time::TimeStamp startTime_;
    uint64_t lastCpuTime_ = 0;
    uint64_t lastWallTime_ = 0;
};

#endif
