#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "infra/hal/pmu/pmu_data.hpp"
#include "service/network_manager/wifi_messages.hpp"

class CmdActor : public Actor{
public:
    CmdActor(std::string name, uint64_t id);
    ~CmdActor() override;

    CmdActor(const CmdActor&) = delete;
    CmdActor& operator=(const CmdActor&) = delete;
    CmdActor(CmdActor&&) = delete;
    CmdActor& operator=(CmdActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    using Handler = std::function<std::string(const std::vector<std::string>&)>;

    std::string dispatch(const std::string& cmd);
    std::string handleActor(const std::vector<std::string>& args);
    std::string handlePmu(const std::vector<std::string>& args);
    std::string handleWifi(const std::vector<std::string>& args);
    std::string handleMetrics(const std::vector<std::string>& args);

    // Actor helpers
    std::string doActorList();
    std::string doActorToggle(bool enable, const std::string& name);

    std::string formatPmuStatus(const PmuData& d);

    // Wifi helpers
    std::string formatApList();
    std::string formatStatus();

    // Metrics helpers
    std::string formatMetricsSnapshot();

    WifiScanResult lastScan_;
    WifiStatusResult lastStatus_;
    ConnHandle pendingConn_ = 0;
    bool pmuStatusPending_ = false;
    std::string lastConnectResult_;
    std::string lastDisconnectResult_;
    std::unordered_map<std::string, Handler> handlers_;
};
