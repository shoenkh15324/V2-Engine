#include "demo_app.hpp"
#include "core/common/log.hpp"
#include "core/common/time.hpp"
#include "core/common/sleep.hpp"
#include <csignal>

DemoApp* DemoApp::sInstance = nullptr;

DemoApp::DemoApp() : actorSystem_(4){
    sInstance = this;
}

DemoApp::~DemoApp(){
    close();
}

void DemoAppActor::handle(const Message& msg){
    std::visit(overloaded{
        [](const TimerExpired& timer){
            V2_LOG_INFO("Timer expired! / Id: %d, Time: %ld", timer.timerId, Time::nowMs());
        },
        [](const auto&){ // ← catch-all: 관심 없는 메시지는 무시
        }
    }, msg);
}

void DemoApp::open(){
    V2_LOG_INFO("Project Name: %s", V2_ENGINE_NAME);
    V2_LOG_INFO("Project Version: v%s", V2_ENGINE_VERSION);
    V2_LOG_INFO("Bulid Data: %s", Time::nowDateString().c_str());
    V2_LOG_INFO("Demo App Open");
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    //
    demoAppActor_ = actorSystem_.createActor<DemoAppActor>("demoApp", 128);
    demoAppActor_->startTimer(TimerExpired{0}, 100, true);
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
