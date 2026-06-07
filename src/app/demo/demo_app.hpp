#pragma once
#include "core/actor_system/actor_system.hpp"
#include "core/actor_system/actor/actor.hpp"

class DemoAppActor : public Actor{
public:
    using Actor::Actor;
    ~DemoAppActor() override = default;
    void handle(const Message& msg) override;
};

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
    static void onSignal(int);
    static DemoApp* sInstance;
    ActorSystem actorSystem_;
    Actor* demoAppActor_ = nullptr;
    bool isRunning_ = false;
};
