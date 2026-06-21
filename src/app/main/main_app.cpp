#include "main_app.hpp"
#include "core/common/config.h"
#include "core/common/log.hpp"
#include "core/common/time.hpp"
#include "core/common/sleep.hpp"
#if V2_ENABLE_IPC_SERVER_ACTOR
#include "service/ipc/ipc_server_actor.hpp"
#endif
#if V2_ENABLE_TICK_ACTOR
#include "service/tick/tick_actor.hpp"
#endif
#include <csignal>

MainApp* MainApp::sInstance = nullptr;

MainApp::MainApp() : actorSystem_(V2_DEFAULT_WORKER_COUNT){
    sInstance = this;
}

MainApp::~MainApp(){
    close();
}

void MainApp::open(){
    setLogLevel(static_cast<LogLevel>(V2_DEFAULT_LOG_LEVEL));
    setLogAppName(std::move(name_));
    V2_LOG_INFO("%s App Open", name_.c_str());
    V2_LOG_INFO("%s App Bulid Data: %s", name_.c_str(), Time::nowDateString().c_str());
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_APP_VERSION);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    //
#if V2_ENABLE_TICK_ACTOR
    actorSystem_.createActor<TickActor>("tick", V2_DEFAULT_MAILBOX_SIZE, V2_DEFAULT_TICK_INTERVAL_MS);
#endif
#if V2_ENABLE_IPC_SERVER_ACTOR
    actorSystem_.createActor<IpcServerActor>("ipcServer", V2_DEFAULT_MAILBOX_SIZE, V2_DEFAULT_IPC_SOCKET_PATH);
#endif
    //
    actorSystem_.start();
}

void MainApp::onSignal(int){
    if(sInstance){
        sInstance->requestStop();
    }
}

void MainApp::requestStop(){
    isRunning_ = false;
    V2_LOG_INFO("");
    actorSystem_.requestStop();
}

void MainApp::run(){
    isRunning_ = true;
    V2_LOG_INFO("%s App Run", name_.c_str());
    actorSystem_.run();
    while(isRunning_){
        Sleep::sleepMs(V2_MAINAPP_MAINLOOP_SLEEP_MS);
    }
}

void MainApp::close(){
    V2_LOG_INFO("%s App Close", name_.c_str());
    isRunning_ = false;
    actorSystem_.stop();
}
