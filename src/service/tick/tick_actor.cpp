#include "tick_actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/common/time/time.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/runtime/scheduler.hpp"

TickActor::TickActor(const std::string& name, uint64_t id, uint64_t tickMs) : Actor(std::move(name), id), tickMs_(tickMs){
    //
}

void TickActor::onStart(){
    startTimer(Tick{}, tickMs_, true);
}

void TickActor::handle(const Message& msg){
    std::visit(overloaded{
        [](const Tick&){
            V2_LOG_INFO("Timer expired! / Time: %ld", Time::nowMs());
        },
        [](const auto&){}
    }, msg);
}
