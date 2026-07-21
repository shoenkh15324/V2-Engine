#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "infra/hal/pmu/i_pmu.hpp"
#include "core/actor_system/messages/network_manager/wifi_messages.hpp"
#include "core/perf/benchmark/benchmark.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>

class ActorSystem;

class CmdActor : public Actor{
public:
    CmdActor(const std::string& name, uint64_t id, ActorSystem* actorSystem);
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
    std::string handleBenchmark(const std::vector<std::string>& args);

    // Actor helpers
    std::string doActorList();
    std::string doActorToggle(bool enable, const std::string& name);

    // Wifi helpers
    std::string formatApList();
    std::string formatStatus();

    // Metrics helpers
    std::string formatMetricsSnapshot();

    // Benchmark helpers
    std::string formatBenchmarkResult(const BenchmarkResult& result);

    std::unordered_map<std::string, Handler> handlers_;
    std::unique_ptr<IPmu> pmu_;
    ActorSystem* actorSystem_ = nullptr;
    WifiScanResult lastScan_;
    WifiStatusResult lastStatus_;
    std::string lastConnectResult_;
    std::string lastDisconnectResult_;
};
