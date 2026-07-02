#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"

class TickActor : public Actor{
public:
    TickActor(const std::string& name, uint64_t id, uint64_t tickMs);

    TickActor(const TickActor&) = delete;
    TickActor& operator=(const TickActor&) = delete;
    TickActor(TickActor&&) = delete;
    TickActor& operator=(TickActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;
    
private:
    uint64_t tickMs_;
};
