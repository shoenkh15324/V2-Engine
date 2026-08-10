#pragma once
#include <cstdint>

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
