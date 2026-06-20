#pragma once
#include "core/actor_system/actor_system.hpp"

class DemoApp{
public:
    DemoApp(const DemoApp&) = delete;
    DemoApp& operator=(const DemoApp&) = delete;
    DemoApp(DemoApp&&) = delete;
    DemoApp& operator=(DemoApp&&) = delete;

    DemoApp();
    ~DemoApp();
    void open();
    void run();
    void close();

private:
    void requestStop();

private:
    static void onSignal(int);
    static DemoApp* sInstance;
    ActorSystem actorSystem_;
    bool isRunning_ = false;
};
