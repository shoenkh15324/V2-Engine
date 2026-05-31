#pragma once
#include "core/actor_system/actor_system.hpp"

class DemoApp{
public:
    DemoApp();
    ~DemoApp();
    void open();
    void run();
    void close();

private:
    static void onSignal(int);
    static DemoApp* sInstance;
    ActorSystem actorSystem_;
    bool isRunning_ = false;
};
