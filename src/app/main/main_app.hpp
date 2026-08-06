#pragma once
#include <memory>
#include <string>
#include <atomic>
#include "core/common/log/log.hpp"
#include "core/common/config/runtime_config.h"
#include "core/common/di/service_container.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "infra/hal/pmu/i_pmu.hpp"
#include "infra/hal/sys/i_sys.hpp"

class MainApp{
public:
    MainApp(const MainApp&) = delete;
    MainApp& operator=(const MainApp&) = delete;
    MainApp(MainApp&&) = delete;
    MainApp& operator=(MainApp&&) = delete;

    MainApp();
    ~MainApp();
    void open();
    void run();
    void close();
    
private:
    void requestStop();
    void configureRuntime();
    void registerServices();
    void createActors();

private:
    Logger logger_;
    Metrics metrics_;
    RuntimeConfig cfg_;
    ServiceContainer di_;
    std::shared_ptr<IPmu> pmu_;
    std::shared_ptr<ISys> sys_;
    std::string name_ = "Main";
    std::atomic<bool> isRunning_{false};
    std::unique_ptr<ITimer> timer_;
    std::unique_ptr<IEventLoop> eventLoop_;
    std::unique_ptr<ActorSystem> actorSystem_;
};
