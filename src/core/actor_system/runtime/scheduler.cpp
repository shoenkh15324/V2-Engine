#include "scheduler.hpp"
#include "core/common/config/platform_config.h"
#include "core/actor_system/actor/actor.hpp"
#include "core/common/log/log.hpp"
#include "dispatcher/dispatcher.hpp"

Scheduler::~Scheduler(){
    stop();
}

int Scheduler::addTimer(Actor* target, Message message, uint64_t timeMs, bool repeating){
    auto sharedMsg = std::make_shared<Message>(std::move(message));
    return timer_.add(timeMs, repeating, [target, sharedMsg](int){
        target->receiveMsg(*sharedMsg);
    });
}

void Scheduler::cancel(int timerId){
    timer_.cancel(timerId);
}

void Scheduler::subscribeTimer(){
#if V2_PLATFORM_LINUX
    if(dispatcher_ && timer_.fd() >= 0){
        dispatcher_->subscribe(timer_.fd(), [this](){ timer_.handleTimerEvent(); });
    }
#endif
}

void Scheduler::unsubscribeTimer(){
#if V2_PLATFORM_LINUX
    if(dispatcher_ && timer_.fd() >= 0){
        dispatcher_->unsubscribe(timer_.fd());
    }
#endif
}

void Scheduler::start(Dispatcher* dispatcher){
    timer_.start();
    dispatcher_ = dispatcher;
    subscribeTimer();
}

void Scheduler::stop(){
    unsubscribeTimer();
    timer_.stop();
}
