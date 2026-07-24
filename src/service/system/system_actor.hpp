#pragma once
#include <vector>
#include <functional>
#include "core/actor_system/actor/actor.hpp"

class SystemActor : public Actor {
public:
    using Callback = std::function<void(int)>;

    SystemActor(const std::string& name, uint64_t id);
    ~SystemActor() override = default;

    SystemActor(const SystemActor&) = delete;
    SystemActor& operator=(const SystemActor&&) = delete;
    SystemActor(SystemActor&&) = delete;
    SystemActor& operator=(SystemActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;
    static void onSignal(int signum, Callback cb);

private:
    int signalPipeFd_ = -1;
};
