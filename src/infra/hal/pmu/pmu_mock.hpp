#pragma once
#include "i_pmu.hpp"

class PmuMock : public IPmu{
public:
    PmuMock() = default;
    ~PmuMock() override = default;

    PmuMock(const PmuMock&) = delete;
    PmuMock& operator=(const PmuMock&) = delete;
    PmuMock(PmuMock&&) = delete;
    PmuMock& operator=(const PmuMock&&) = delete;

    int open() override { return 0; };
    int close() override { return 0; };
    bool isOpen() const override { return true; }
    int readPmuData(PmuData& data) override {
        data.clockArmHz = 1500000000;
        data.clockCoreHz = 500000000;
        data.clockV3dHz = 300000000;
        data.tempCelsius = 42.5f;
        data.voltCore = 0.88f;
        data.currentVddCoreA = 0.75f;
        data.memArmMb = 1024;
        data.memGpuMb = 16;
        data.throttled = 0;
        return 0;
    }
};
