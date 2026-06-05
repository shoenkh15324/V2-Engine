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
        }
    }, msg);
}

void DemoApp::open(){
    V2_LOG_INFO("Demo App Open");
    V2_LOG_INFO("Bulid Data: %s", Time::nowDateString().c_str());
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    //
    demoAppActor_ = actorSystem_.createActor<DemoAppActor>("demoApp", 128);
    actorSystem_.scheduler().addTimer(demoAppActor_, TimerExpired{0}, 100, true);
    //
    actorSystem_.start();
}

void DemoApp::onSignal(int){ V2_LOG_INFO("");
    if(sInstance){
        sInstance->isRunning_ = false;
    }
}

void DemoApp::run(){
    isRunning_ = true;
    V2_LOG_INFO("Demo App Run");
    while(isRunning_){
        Sleep::sleepMs(100);
    }
}

void DemoApp::close(){
    isRunning_ = false;
    V2_LOG_INFO("Demo App Close");
    actorSystem_.stop();
}
