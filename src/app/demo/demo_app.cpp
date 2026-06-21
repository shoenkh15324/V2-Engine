#include "demo_app.hpp"
#include "core/common/config.h"
#include "core/common/log.hpp"
#include "core/common/time.hpp"
#include "core/common/sleep.hpp"
#if V2_ENABLE_IPC
#include "service/ipc/ipc_server_actor.hpp"
#endif
#if V2_ENABLE_TICK
#include "service/tick/tick_actor.hpp"
#endif
#include <csignal>

DemoApp* DemoApp::sInstance = nullptr;

DemoApp::DemoApp() : actorSystem_(V2_DEFAULT_WORKER_COUNT){
    sInstance = this;
}

DemoApp::~DemoApp(){
    close();
}

void DemoApp::open(){
    setLogAppName(std::move(name_));
    V2_LOG_INFO("%s App Open", name_.c_str());
    V2_LOG_INFO("%s App Bulid Data: %s", name_.c_str(), Time::nowDateString().c_str());
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_APP_VERSION);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    //
#if V2_ENABLE_TICK
    actorSystem_.createActor<TickActor>("tick", V2_DEFAULT_MAILBOX_SIZE, V2_DEFAULT_TICK_INTERVAL_MS);
#endif
#if V2_ENABLE_IPC
    actorSystem_.createActor<IpcServerActor>("ipcServer", V2_DEFAULT_MAILBOX_SIZE, V2_DEFAULT_IPC_SOCKET_PATH);
#endif
    //
    actorSystem_.start();
}

void DemoApp::onSignal(int){
    if(sInstance){
        sInstance->requestStop();
    }
}

void DemoApp::requestStop(){
    isRunning_ = false;
    V2_LOG_INFO("");
    actorSystem_.requestStop();
}

void DemoApp::run(){
    isRunning_ = true;
    V2_LOG_INFO("Demo App Run");
    actorSystem_.run();
    while(isRunning_){
        Sleep::sleepMs(V2_DEMO_MAINLOOP_SLEEP_MS);
    }
}

void DemoApp::close(){
    isRunning_ = false;
    actorSystem_.stop();
}
