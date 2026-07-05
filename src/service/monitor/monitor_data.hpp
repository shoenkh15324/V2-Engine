#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

struct ActorInfo{
    std::string name;
    uint64_t id;
    size_t mailboxCount;
    size_t mailboxCapacity;
    int state = 0;
    bool essential = false;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ActorInfo,
    name,
    id,
    mailboxCount,
    mailboxCapacity,
    state,
    essential
)

struct SystemResources{
    uint64_t memoryRssKb = 0;
    uint64_t memoryTotalKb = 0;
    float cpuPercent = 0.0f;
    uint64_t uptimeMs = 0;
    uint64_t threadCount = 0;
    uint64_t vmPeakKb = 0;
    uint64_t vmHwmKb = 0;
    uint64_t vmSwapKb = 0;
    float loadAvg1 = 0.0f;
    float loadAvg5 = 0.0f;
    float loadAvg15 = 0.0f;
    uint64_t sysMemTotalKb = 0;
    uint64_t sysMemAvailKb = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SystemResources,
    memoryRssKb,
    memoryTotalKb,
    cpuPercent,
    uptimeMs,
    threadCount,
    vmPeakKb,
    vmHwmKb,
    vmSwapKb,
    loadAvg1, 
    loadAvg5,
    loadAvg15,
    sysMemTotalKb,
    sysMemAvailKb
)

struct MonitorSnapshot{
    uint64_t timestampMs = 0;
    int clientCount = 0;
    std::vector<ActorInfo> actors;
    SystemResources resources;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MonitorSnapshot,
    timestampMs,
    clientCount,
    actors,
    resources
)
