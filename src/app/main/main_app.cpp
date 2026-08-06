#include "main_app.hpp"
#include <csignal>
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/time/sleep.hpp"
#include "core/common/timer/i_timer.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/config/platform_config.h"
#include "infra/hal/pmu/pmu_mock.hpp"
#include "infra/hal/pmu/pmu_rsp5.hpp"
#include "infra/hal/sys/sys_mock.hpp"
#include "infra/hal/sys/sys_linux.hpp"
#include "infra/platform/linux/signal_handler.hpp"
#include "service/system/system_actor.hpp"
#include "service/ipc/ipc_server_actor.hpp"
#include "service/tick/tick_actor.hpp"
#include "service/monitor/monitor_actor.hpp"
#include "service/dbus/dbus_actor.hpp"
#include "service/device_manager/device_manager_actor.hpp"
#include "service/network_manager/network_manager_actor.hpp"
#include "service/cmd/cmd_actor.hpp"

#if V2_PLATFORM_LINUX
    #include "infra/platform/linux/event_loop_epoll.hpp"
    #include "infra/platform/linux/timer_linux.hpp"
#endif


MainApp::MainApp() = default;

MainApp::~MainApp(){
    close();
}

void MainApp::open(){
    V2_LOG_INFO("{} App Open", name_.c_str());
    V2_LOG_INFO("{} App Bulid Data: {}", name_.c_str(), Time::nowDateString().c_str());
    cfg_ = RuntimeConfig::loadFromFile(V2_CONFIG_DIR "/v2_main.json");

    configureRuntime();
    registerServices();
    createActors();
    actorSystem_->start();
}

void MainApp::run(){
    isRunning_.store(true, std::memory_order_release);
    V2_LOG_INFO("{} App Run", name_.c_str());
    if(actorSystem_) actorSystem_->run();
    while(isRunning_.load(std::memory_order_relaxed)){
        Sleep::sleepMs(cfg_.mainLoopSleepMs);
    }
}

void MainApp::close(){
    V2_LOG_INFO("{} App Close", name_.c_str());
    isRunning_.store(false, std::memory_order_release);
    if(actorSystem_) actorSystem_->stop();
    actorSystem_.reset();
    if(sys_){ sys_->close(); sys_.reset(); }
    if(pmu_){ pmu_->close(); pmu_.reset(); }
}

void MainApp::requestStop(){
    isRunning_.store(false, std::memory_order_release);
    V2_LOG_INFO("");
    if(actorSystem_) actorSystem_->requestStop();
}

void MainApp::configureRuntime(){
    // Set Log
    setLogLevel(static_cast<LogLevel>(cfg_.logLevel));
    setLogAppName(std::move(name_));
    setLogFile("log/v2_main.log");

    // Set Metric
    setActiveMetrics(&metrics_);
    metrics_.setEnabled(cfg_.enableMetrics);

    // Set Signal
    SystemActor::onSignal(SIGINT, [this](int){ requestStop(); });
    SystemActor::onSignal(SIGTERM, [this](int){ requestStop(); });

#if V2_PLATFORM_LINUX
    eventLoop_ = std::make_unique<EventLoopEpoll>(cfg_.epollMaxEvents, cfg_.epollWaitTimeoutMs);
    timer_ = std::make_unique<LinuxTimer>(eventLoop_.get());
#endif
}

void MainApp::registerServices(){
#if V2_PLATFORM_LINUX && defined(__aarch64__)
    di_.bind<IPmu, PmuRsp5>(Lifetime::Singleton);
#else
    di_.bind<IPmu, PmuMock>(Lifetime::Singleton);
#endif
#if V2_PLATFORM_LINUX
    di_.bind<ISys, SysLinux>(Lifetime::Singleton);
#else
    di_.bind<ISys, SysMock>(Lifetime::Singleton);
#endif
    pmu_ = di_.resolve<IPmu>();
    sys_ = di_.resolve<ISys>();
    pmu_->open();
    sys_->open();
}

void MainApp::createActors(){
    actorSystem_ = createDefaultActorSystem({cfg_.workerCount, cfg_.workerMaxBatch, static_cast<size_t>(cfg_.mailboxSize)}, std::move(eventLoop_), std::move(timer_));
    actorSystem_->createActor<SystemActor>("system_actor", cfg_.mailboxSize)->setEssential(true);
    actorSystem_->createActor<CmdActor>("cmd_actor", cfg_.mailboxSize, pmu_.get())->setEssential(true);
    actorSystem_->createActor<DeviceManagerActor>("device_manager", cfg_.mailboxSize)->setEssential(true);
    if(cfg_.enableTick) actorSystem_->createActor<TickActor>("tick", cfg_.mailboxSize, cfg_.tickIntervalMs)->setEssential(false);
    if(cfg_.enableMonitor) actorSystem_->createActor<MonitorActor>("monitor", cfg_.mailboxSize, MonitorConfig{cfg_.monitorSocketPath, cfg_.monitorBacklog, cfg_.monitorPollIntervalMs}, sys_.get(), pmu_.get())->setEssential(true);
#if V2_PLATFORM_LINUX
    if(cfg_.enableIpcServer) actorSystem_->createActor<IpcServerActor>("ipc_server", cfg_.mailboxSize, cfg_.ipcSocketPath, cfg_.udsBacklog, cfg_.ipcRecvBufferSize)->setEssential(true);
    if(cfg_.enableDbus) actorSystem_->createActor<DbusActor>("dbus_actor", cfg_.mailboxSize, cfg_.dbusBusName, cfg_.dbusObjectPath, cfg_.dbusInterfaceName)->setEssential(true);
    if(cfg_.enableDbus && cfg_.enableNetworkManager) actorSystem_->createActor<NetworkManagerActor>("network_manager", cfg_.mailboxSize)->setEssential(false);
#endif
}
