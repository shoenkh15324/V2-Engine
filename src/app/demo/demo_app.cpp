#include "demo_app.hpp"
#include "core/common/log.hpp"
#include "core/common/time.hpp"
#include "core/common/sleep.hpp"
#include "service/ipc/ipc_server_actor.hpp"
#include "service/tick/tick_actor.hpp"
#include <csignal>

DemoApp* DemoApp::sInstance = nullptr;

DemoApp::DemoApp() : actorSystem_(4){
    sInstance = this;
}

DemoApp::~DemoApp(){
    close();
}

void DemoApp::open(){
    V2_LOG_INFO("Project Name: %s", V2_ENGINE_NAME);
    V2_LOG_INFO("Project Version: v%s", V2_ENGINE_VERSION);
    V2_LOG_INFO("Bulid Data: %s", Time::nowDateString().c_str());
    V2_LOG_INFO("Demo App Open");
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    //
    actorSystem_.createActor<TickActor>("tick", 32, 100);
    actorSystem_.createActor<IpcServerActor>("ipcServer", 128, "/tmp/v2_ipc.sock");
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
        Sleep::sleepMs(100);
    }
}

void DemoApp::close(){
    isRunning_ = false;
    actorSystem_.stop();
}
