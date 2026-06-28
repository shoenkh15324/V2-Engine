#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct ActorInfo{
    std::string name;
    uint64_t id;
    size_t mailboxCount;
    size_t mailboxCapacity;
};

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

struct MonitorSnapshot{
    uint64_t timestampMs = 0;
    int clientCount = 0;
    std::vector<ActorInfo> actors;
    SystemResources resources;
};

std::string serializeSnapshot(const MonitorSnapshot& snap);
bool parseSnapshotLine(const std::string& line, MonitorSnapshot& snap);
bool isSnapshotBegin(const std::string& line);
bool isSnapshotEnd(const std::string& line);
