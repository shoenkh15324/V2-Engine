#include "scheduler.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/common/log/log.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"

Scheduler::~Scheduler(){
    stop();
}

void Scheduler::start(IEventLoop* eventLoop){
    timer_.start();
    eventLoop_ = eventLoop;
    subscribeTimer();
}

void Scheduler::stop(){
    unsubscribeTimer();
    timer_.stop();
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
    if(eventLoop_ && (timer_.fd() >= 0)){
        eventLoop_->subscribe(timer_.fd(), [this](){ timer_.handleTimerEvent(); });
    }
}

void Scheduler::unsubscribeTimer(){
    if(eventLoop_ && (timer_.fd() >= 0)){
        eventLoop_->unsubscribe(timer_.fd());
    }
}
