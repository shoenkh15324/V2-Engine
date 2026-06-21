#pragma once
#include <string>
#include "core/actor_system/actor_system.hpp"

class CliApp{
public:
    CliApp(const CliApp&) = delete;
    CliApp& operator=(const CliApp&) = delete;
    CliApp(CliApp&&) = delete;
    CliApp& operator=(CliApp&&) = delete;

    CliApp();
    ~CliApp();
    void open();
    void run();
    void close();

private:
    void requestStop();

private:
    static void onSignal(int);
    static CliApp* sInstance;
    ActorSystem actorSystem_;
    bool isRunning_ = false;
    std::string name_ = "Cli";
};
