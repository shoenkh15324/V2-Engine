#include "main_app.hpp"
#include <csignal>
#include "core/common/log/log.hpp"
#include "core/common/util/debug.hpp"
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
#include "service/system_manager/system_manager_actor.hpp"
#include "service/ipc/ipc_server_actor.hpp"
#include "service/tick/tick_actor.hpp"
#include "service/monitor/monitor_actor.hpp"
#include "service/monitor/monitor_bridge_actor.hpp"
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
    setActiveLogger(&logger_);
    logger_.setLevel(static_cast<LogLevel>(cfg_.logLevel));
    logger_.setLogFile("log/v2_main.log");

    // Set Metric
    setActiveMetrics(&metrics_);
    metrics_.setEnabled(cfg_.enableMetrics);

    // Set Memory
    initGlobalMemoryPoolConfig(cfg_.memorySlabSize, cfg_.memoryMaxPoolAllocSize);

    // Set Signal
    SystemManagerActor::onSignal(SIGINT, [this](int){ requestStop(); });
    SystemManagerActor::onSignal(SIGTERM, [this](int){ requestStop(); });

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
    ActorSystemConfig sysConfig;
    sysConfig.numWorkers = cfg_.workerCount;
    sysConfig.maxBatch = cfg_.workerMaxBatch;
    sysConfig.busyStealIntervalUs = cfg_.busyStealIntervalUs;
    sysConfig.idleStealIntervalUs = cfg_.idleStealIntervalUs;
    sysConfig.parkSpinNs = cfg_.parkSpinNs;
    sysConfig.tokenGraceNs = cfg_.tokenGraceNs;
    sysConfig.defaultMailboxSize = cfg_.defaultMailboxSize;
    sysConfig.dispatcherQueueCapacity = cfg_.dispatcherQueueCapacity;
    sysConfig.dispatcherHighWatermark = cfg_.dispatcherHighWatermark;
    sysConfig.supervisorMaxRestarts = cfg_.supervisorMaxRestarts;
    sysConfig.supervisorDefaultStrategy = cfg_.supervisorDefaultStrategy;
    sysConfig.deadLetterQueueCapacity = cfg_.deadLetterQueueCapacity;
    actorSystem_ = createDefaultActorSystem(sysConfig, std::move(eventLoop_), std::move(timer_));
    actorSystem_->createActor<SystemManagerActor>("system_manager", cfg_.defaultMailboxSize, sys_.get(), cfg_.monitorPollIntervalMs)->setEssential(true);
    actorSystem_->createActor<CmdActor>("cmd", cfg_.defaultMailboxSize)->setEssential(true);
    if(cfg_.enableDeviceManager) actorSystem_->createActor<DeviceManagerActor>("device_manager", cfg_.defaultMailboxSize, pmu_.get(), cfg_.monitorPollIntervalMs)->setEssential(true);
    if(cfg_.enableTick) actorSystem_->createActor<TickActor>("tick", cfg_.defaultMailboxSize, cfg_.tickIntervalMs)->setEssential(false);
    if(cfg_.enableMonitor){
        actorSystem_->createActor<MonitorActor>("monitor", cfg_.defaultMailboxSize)->setEssential(true);
#if V2_PLATFORM_LINUX
        actorSystem_->createActor<MonitorBridgeActor>("monitor_bridge", cfg_.defaultMailboxSize, cfg_.monitorSocketPath, cfg_.monitorBacklog)->setEssential(true);
#endif
    }
#if V2_PLATFORM_LINUX
    if(cfg_.enableIpcServer) actorSystem_->createActor<IpcServerActor>("ipc_server", cfg_.defaultMailboxSize, cfg_.ipcSocketPath, cfg_.udsBacklog, cfg_.ipcRecvBufferSize)->setEssential(true);
    if(cfg_.enableDbus) actorSystem_->createActor<DbusActor>("dbus", cfg_.defaultMailboxSize, cfg_.dbusBusName, cfg_.dbusObjectPath, cfg_.dbusInterfaceName)->setEssential(true);
    if(cfg_.enableDbus && cfg_.enableNetworkManager) actorSystem_->createActor<NetworkManagerActor>("network_manager", cfg_.defaultMailboxSize)->setEssential(false);
#endif
}
