#pragma once
#include "i_sys.hpp"

class SysMock : public ISys{
public:
    int open() override { return 0; }
    int close() override { return 0; }
    int collect(SystemResources& data) override {
        data.memoryRssKb = 123456;
        data.memoryTotalKb = 789012;
        data.cpuPercent = 12.5f;
        data.uptimeMs = 3600000;
        data.threadCount = 42;
        data.vmPeakKb = 999999;
        data.vmHwmKb = 500000;
        data.vmSwapKb = 0;
        data.loadAvg1 = 0.5f;
        data.loadAvg5 = 0.3f;
        data.loadAvg15 = 0.2f;
        data.sysMemTotalKb = 8123456;
        data.sysMemAvailKb = 4123456;
        return 0;
    }
};
