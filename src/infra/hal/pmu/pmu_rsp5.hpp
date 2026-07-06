#pragma once
#include "i_pmu.hpp"
#include <string>

class PmuRsp5 : public IPmu{
public:
    PmuRsp5() = default;
    ~PmuRsp5() override;

    PmuRsp5(const PmuRsp5&) = delete;
    PmuRsp5& operator=(const PmuRsp5&) = delete;
    PmuRsp5(PmuRsp5&&) = delete;
    PmuRsp5& operator=(PmuRsp5&&) = delete;

    int open() override;
    int close() override;
    bool isOpen() const override;
    int readPmuData(PmuData& data) override;

private:
    static std::string exec(const char* cmd);
    static uint64_t parseClock(const std::string& out);
    static uint64_t parseMemMb(const std::string& out);
    static uint64_t parseThrottled(const std::string& out);
    static float parseTemp(const std::string& out);
    static float parseVolt(const std::string& out);
    static float parseCurrent(const std::string& out);

    bool opened_ = false;
};
