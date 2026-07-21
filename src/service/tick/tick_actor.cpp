#include "tick_actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/common/time/time.hpp"
#include "core/actor_system/actor/i_actor_runtime.hpp"
#include "core/actor_system/runtime/scheduler.hpp"

TickActor::TickActor(const std::string& name, uint64_t id, uint64_t tickMs) : Actor(std::move(name), id), tickMs_(tickMs){
    //
}

int TickActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    startTimer(Tick{}, tickMs_, true);
    //
    state_ = Opened;
    return Ok;
}

int TickActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    auto ids = timerIds_;
    for(int id : ids) cancelTimer(id);
    //
    state_ = Closed;
    return Ok;
}

void TickActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [](const Tick&){ V2_LOG_INFO("Timer expired! / Time: %ld", Time::nowMs()); },
        [](const auto&){}
    }, msg);
}
