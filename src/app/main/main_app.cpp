#include "main_app.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/time/sleep.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/os/signal_handler.hpp"
#include "service/system/system_actor.hpp"
#include "service/ipc/ipc_server_actor.hpp"
#include "service/tick/tick_actor.hpp"
#include "service/monitor/monitor_actor.hpp"
#include "service/dbus/dbus_actor.hpp"
#include "service/device_manager/device_manager_actor.hpp"
#include "service/network_manager/network_manager_actor.hpp"
#include "service/cmd/cmd_actor.hpp"
#include <csignal>

MainApp::MainApp() = default;

MainApp::~MainApp(){
    close();
}

void MainApp::open(){
    cfg_ = RuntimeConfig::loadFromFile(V2_CONFIG_DIR "/v2_main.json");
    setLogLevel(static_cast<LogLevel>(cfg_.logLevel));
    setLogAppName(std::move(name_));
    V2_LOG_INFO("%s App Open", name_.c_str());
    V2_LOG_INFO("%s App Bulid Data: %s", name_.c_str(), Time::nowDateString().c_str());
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_ENGINE_VERSION);
    //
    Metrics::setEnabled(cfg_.enableMetrics);
    SystemActor::onSignal(SIGINT, [this](int){ requestStop(); });
    SystemActor::onSignal(SIGTERM, [this](int){ requestStop(); });

    actorSystem_ = std::make_unique<ActorSystem>(cfg_.workerCount, cfg_.workerMaxBatch, cfg_.epollMaxEvents, cfg_.epollWaitTimeoutMs);
    actorSystem_->createActor<SystemActor>("system_actor", cfg_.mailboxSize)->setEssential(true);
    actorSystem_->createActor<CmdActor>("cmd_actor", cfg_.mailboxSize)->setEssential(true);
    actorSystem_->createActor<DeviceManagerActor>("device_manager", cfg_.mailboxSize)->setEssential(true);
    if(cfg_.enableTick) actorSystem_->createActor<TickActor>("tick", cfg_.mailboxSize, cfg_.tickIntervalMs)->setEssential(false);
    if(cfg_.enableMonitor) actorSystem_->createActor<MonitorActor>("monitor", cfg_.mailboxSize, MonitorConfig{cfg_.monitorSocketPath, cfg_.monitorBacklog, cfg_.monitorPollIntervalMs})->setEssential(true);
#if V2_PLATFORM_LINUX
    if(cfg_.enableIpcServer) actorSystem_->createActor<IpcServerActor>("ipc_server", cfg_.mailboxSize, cfg_.ipcSocketPath, cfg_.udsBacklog, cfg_.ipcRecvBufferSize)->setEssential(true);
    if(cfg_.enableDbus) actorSystem_->createActor<DbusActor>("dbus_actor", cfg_.mailboxSize, cfg_.dbusBusName, cfg_.dbusObjectPath, cfg_.dbusInterfaceName)->setEssential(true);
    if(cfg_.enableDbus && cfg_.enableNetworkManager) actorSystem_->createActor<NetworkManagerActor>("network_manager", cfg_.mailboxSize)->setEssential(false);
#endif
    //
    actorSystem_->start();
}

void MainApp::requestStop(){
    isRunning_.store(false);
    V2_LOG_INFO("");
    if(actorSystem_) actorSystem_->requestStop();
}

void MainApp::run(){
    isRunning_.store(true);
    V2_LOG_INFO("%s App Run", name_.c_str());
    if(actorSystem_) actorSystem_->run();
    while(isRunning_.load()){
        Sleep::sleepMs(cfg_.mainLoopSleepMs);
    }
}

void MainApp::close(){
    V2_LOG_INFO("%s App Close", name_.c_str());
    isRunning_.store(false);
    if(actorSystem_) actorSystem_->stop();
    actorSystem_.reset();
}
