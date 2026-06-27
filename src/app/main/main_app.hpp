#pragma once
#include <memory>
#include <string>
#include "core/actor_system/actor_system.hpp"
#include "core/common/runtime_config.h"

class MainApp{
public:
    MainApp(const MainApp&) = delete;
    MainApp& operator=(const MainApp&) = delete;
    MainApp(MainApp&&) = delete;
    MainApp& operator=(MainApp&&) = delete;

    MainApp();
    ~MainApp();
    void open();
    void run();
    void close();
    
private:
    void requestStop();

private:
    static void onSignal(int);
    static MainApp* sInstance;
    RuntimeConfig cfg_;
    std::unique_ptr<ActorSystem> actorSystem_;
    bool isRunning_ = false;
    std::string name_ = "Main";
};
