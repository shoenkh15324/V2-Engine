#include "cli_app.hpp"
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

CliApp* CliApp::sInstance = nullptr;

CliApp::CliApp() : actorSystem_(V2_DEFAULT_WORKER_COUNT){
    sInstance = this;
}

CliApp::~CliApp(){
    close();
}

void CliApp::open(){
    setLogAppName(std::move(name_));
    V2_LOG_INFO("%s App Open", name_.c_str());
    V2_LOG_INFO("%s App Bulid Data: %s", name_.c_str(), Time::nowDateString().c_str());
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_APP_VERSION);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    //
    //
    actorSystem_.start();
}

void CliApp::onSignal(int){
    if(sInstance){
        sInstance->requestStop();
    }
}

void CliApp::requestStop(){
    isRunning_ = false;
    V2_LOG_INFO("");
    actorSystem_.requestStop();
}

void CliApp::run(){
    isRunning_ = true;
    V2_LOG_INFO("%s App Run", name_.c_str());
    actorSystem_.run();
    while(isRunning_){
        Sleep::sleepMs(100);
    }
}

void CliApp::close(){
    V2_LOG_INFO("%s App Close", name_.c_str());
    isRunning_ = false;
    actorSystem_.stop();
}
