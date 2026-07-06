#pragma once
#include "i_sys.hpp"
#include <deque>

class SysLinux : public ISys{
public:
    SysLinux() = default;
    ~SysLinux() override;

    int open() override;
    int close() override;
    int collect(SystemResources& data) override;

private:
    struct CpuSample{
        uint64_t cpuNs;
        uint64_t wallMs;
    };

    std::deque<CpuSample> cpuHistory_;
    static constexpr uint64_t kCpuWindowMs = 1000;
};
