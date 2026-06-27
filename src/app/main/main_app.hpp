#pragma once
#include <memory>
#include <string>
#include <atomic>
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
    std::unique_ptr<ActorSystem> actorSystem_;
    std::atomic<bool> isRunning_{false};
    std::string name_ = "Main";
    RuntimeConfig cfg_;
};
