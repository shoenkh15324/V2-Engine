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
