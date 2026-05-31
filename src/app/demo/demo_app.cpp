#include "demo_app.hpp"
#include "core/common/log.hpp"
#include "core/osal/sleep/sleep.hpp"
#include "core/common/time.hpp"
#include <csignal>

DemoApp* DemoApp::sInstance = nullptr;

DemoApp::DemoApp() : actorSystem_(4){
    sInstance = this;
}

DemoApp::~DemoApp(){
    close();
}

void DemoApp::open(){
    V2_LOG_INFO("Demo App Open");
    V2_LOG_INFO("Bulid Data: %s", Time::nowDateString().c_str());
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    // 여기에 actor 생성
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
        Sleep().sleepMs(100);
    }
}

void DemoApp::close(){
    isRunning_ = false;
    V2_LOG_INFO("Demo App Close");
    actorSystem_.stop();
}
