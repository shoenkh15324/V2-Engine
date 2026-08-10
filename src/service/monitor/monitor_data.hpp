#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "infra/hal/pmu/pmu_data.hpp"
#include "infra/hal/sys/system_data.hpp"

struct ActorInfo{
    std::string name;
    uint64_t id;
    size_t mailboxCount;
    size_t mailboxCapacity;
    int state = 0;
    bool essential = false;
};

struct MonitorSnapshot{
    uint64_t timestampMs = 0;
    int clientCount = 0;
    std::vector<ActorInfo> actors;
    SystemResources sysResources;
    PmuData pmuData;
};

// ── JSON 매핑 (ADL to_json — 키는 TUI/CLI와 호환되는 기존 이름) ─────────
inline void to_json(nlohmann::json& j, const SystemResources& r){
    j = nlohmann::json{
        {"memoryRssKb", r.memoryRssKb},
        {"memoryTotalKb", r.memoryTotalKb},
        {"cpuPercent", r.cpuPercent},
        {"uptimeMs", r.uptimeMs},
        {"threadCount", r.threadCount},
        {"vmPeakKb", r.vmPeakKb},
        {"vmHwmKb", r.vmHwmKb},
        {"vmSwapKb", r.vmSwapKb},
        {"loadAvg1", r.loadAvg1},
        {"loadAvg5", r.loadAvg5},
        {"loadAvg15", r.loadAvg15},
        {"sysMemTotalKb", r.sysMemTotalKb},
        {"sysMemAvailKb", r.sysMemAvailKb}
    };
}

inline void to_json(nlohmann::json& j, const PmuData& p){
    j = nlohmann::json{
        {"clockArmHz", p.clockArmHz},
        {"clockCoreHz", p.clockCoreHz},
        {"clockV3dHz", p.clockV3dHz},
        {"memArmMb", p.memArmMb},
        {"memGpuMb", p.memGpuMb},
        {"throttled", p.throttled},
        {"tempCelsius", p.tempCelsius},
        {"voltCore", p.voltCore},
        {"currentVddCoreA", p.currentVddCoreA}
    };
}

inline void to_json(nlohmann::json& j, const ActorInfo& a){
    j = nlohmann::json{
        {"name", a.name},
        {"id", a.id},
        {"mailboxCount", a.mailboxCount},
        {"mailboxCapacity", a.mailboxCapacity},
        {"state", a.state},
        {"essential", a.essential}
    };
}

inline void to_json(nlohmann::json& j, const MonitorSnapshot& s){
    j = nlohmann::json{
        {"timestampMs", s.timestampMs},
        {"clientCount", s.clientCount},
        {"actors", s.actors},
        {"resources", s.sysResources},
        {"pmu", s.pmuData}
    };
}

inline void from_json(const nlohmann::json& j, SystemResources& r){
    r.memoryRssKb    = j.value("memoryRssKb", 0);
    r.memoryTotalKb  = j.value("memoryTotalKb", 0);
    r.cpuPercent     = j.value("cpuPercent", 0.0f);
    r.uptimeMs       = j.value("uptimeMs", 0);
    r.threadCount    = j.value("threadCount", 0);
    r.vmPeakKb       = j.value("vmPeakKb", 0);
    r.vmHwmKb        = j.value("vmHwmKb", 0);
    r.vmSwapKb       = j.value("vmSwapKb", 0);
    r.loadAvg1       = j.value("loadAvg1", 0.0f);
    r.loadAvg5       = j.value("loadAvg5", 0.0f);
    r.loadAvg15      = j.value("loadAvg15", 0.0f);
    r.sysMemTotalKb  = j.value("sysMemTotalKb", 0);
    r.sysMemAvailKb  = j.value("sysMemAvailKb", 0);
}

inline void from_json(const nlohmann::json& j, PmuData& p){
    p.clockArmHz       = j.value("clockArmHz", 0);
    p.clockCoreHz      = j.value("clockCoreHz", 0);
    p.clockV3dHz       = j.value("clockV3dHz", 0);
    p.memArmMb         = j.value("memArmMb", 0);
    p.memGpuMb         = j.value("memGpuMb", 0);
    p.throttled        = j.value("throttled", 0);
    p.tempCelsius      = j.value("tempCelsius", 0.0f);
    p.voltCore         = j.value("voltCore", 0.0f);
    p.currentVddCoreA  = j.value("currentVddCoreA", 0.0f);
}

inline void from_json(const nlohmann::json& j, ActorInfo& a){
    a.name           = j.value("name", std::string{});
    a.id             = j.value("id", 0);
    a.mailboxCount   = j.value("mailboxCount", 0);
    a.mailboxCapacity= j.value("mailboxCapacity", 0);
    a.state          = j.value("state", 0);
    a.essential      = j.value("essential", false);
}

inline void from_json(const nlohmann::json& j, MonitorSnapshot& s){
    s.timestampMs = j.value("timestampMs", 0);
    s.clientCount = j.value("clientCount", 0);
    j.at("actors").get_to(s.actors);
    j.at("resources").get_to(s.sysResources);
    j.at("pmu").get_to(s.pmuData);
}
