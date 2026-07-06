#include "pmu_rsp5.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <cstdint>
#include <memory>
#include <array>
#include <cstring>
#include <cerrno>

PmuRsp5::~PmuRsp5(){
    close();
}

int PmuRsp5::open(){
    opened_ = true;
    return Ok;
}

int PmuRsp5::close(){
    opened_ = false;
    return Ok;
}

bool PmuRsp5::isOpen() const {
    return opened_;
}

int PmuRsp5::readPmuData(PmuData& data){
    auto s = exec("vcgencmd measure_clock arm");
    data.clockArmHz = s.empty() ? 0 : parseClock(s);
    s = exec("vcgencmd measure_clock core");
    data.clockCoreHz = s.empty() ? 0 : parseClock(s);
    s = exec("vcgencmd measure_clock v3d");
    data.clockV3dHz = s.empty() ? 0 : parseClock(s);
    s = exec("vcgencmd measure_temp");
    data.tempCelsius = s.empty() ? 0.0f : parseTemp(s);
    s = exec("vcgencmd measure_volts core");
    data.voltCore = s.empty() ? 0.0f : parseVolt(s);
    s = exec("vcgencmd pmic_read_adc VDD_CORE_A");
    data.currentVddCoreA = s.empty() ? 0.0f : parseCurrent(s);
    s = exec("vcgencmd get_mem arm");
    data.memArmMb = s.empty() ? 0 : parseMemMb(s);
    s = exec("vcgencmd get_mem gpu");
    data.memGpuMb = s.empty() ? 0 : parseMemMb(s);
    s = exec("vcgencmd get_throttled");
    data.throttled = s.empty() ? 0 : parseThrottled(s);
    return Ok;
}

std::string PmuRsp5::exec(const char* cmd){
    std::array<char, 256> buf{};
    std::string result;

    auto pipe = popen(cmd, "r");
    if(!pipe) return {};
    while(fgets(buf.data(), buf.size(), pipe) != nullptr){
        result += buf.data();
    }
    pclose(pipe);

    // trim trailing newline/whitespace
    while(!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')){
        result.pop_back();
    }
    return result;
}

uint64_t PmuRsp5::parseClock(const std::string& out){
    // "frequency(48)=1500000000"
    auto eq = out.find('=');
    if(eq == std::string::npos) return 0.0f;
    return std::stof(out.substr(eq + 1));
}

uint64_t PmuRsp5::parseMemMb(const std::string& out){
    // "arm=1024M" or "gpu=16M"
    auto eq = out.find('=');
    if(eq == std::string::npos) return 0.0f;
    auto val = out.substr(eq + 1);
    // string trailing 'M'
    if(!val.empty() && val.back() == 'M') val.pop_back();
    return std::stoull(val);
}

uint64_t PmuRsp5::parseThrottled(const std::string& out){
    // "throttled=0x0"
    auto eq = out.find('=');
    if(eq == std::string::npos) return 0;
    return std::stoull(out.substr(eq + 1), nullptr, 16);
}

float PmuRsp5::parseTemp(const std::string& out){
    // "temp=42.3'C"
    auto eq = out.find('=');
    if(eq == std::string::npos) return 0.0f;
    return std::stof(out.substr(eq + 1));
}

float PmuRsp5::parseVolt(const std::string& out){
    // "volt=0.88V"
    auto eq = out.find('=');
    if(eq == std::string::npos) return 0.0f;
    return std::stof(out.substr(eq + 1));
}

float PmuRsp5::parseCurrent(const std::string& out){
    // e.g. "VDD_CORE_A=0.75A" or "0.75"
    auto eq = out.find('=');
    auto val = (eq == std::string::npos) ? out : out.substr(eq + 1);
    return std::stof(val);
}
