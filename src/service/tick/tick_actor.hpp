#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config.h"

#if V2_ENABLE_TICK
class TickActor : public Actor{
public:
    TickActor(const std::string& name, uint64_t id, uint64_t tickMs);

    TickActor(const TickActor&) = delete;
    TickActor& operator=(const TickActor&) = delete;
    TickActor(TickActor&&) = delete;
    TickActor& operator=(TickActor&&) = delete;

    void onStart() override;
    void handle(const Message& msg) override;
    
private:
    uint64_t tickMs_;
};
#endif
