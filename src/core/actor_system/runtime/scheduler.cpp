#include "scheduler.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/common/log.hpp"

#ifdef __linux__
#include "dispatcher.hpp"
#endif

Scheduler::Scheduler() = default;

Scheduler::~Scheduler(){
    stop();
}

int Scheduler::addTimer(Actor* target, Message message, uint64_t delayMs, bool repeating){
    auto sharedMsg = std::make_shared<Message>(std::move(message));
    return timer_.add(delayMs, repeating, [target, sharedMsg](int){
        target->receiveMsg(*sharedMsg);
    });
}

void Scheduler::cancel(int timerId){
    timer_.cancel(timerId);
}

void Scheduler::start(Dispatcher* dispatcher){
    timer_.start();
#ifdef __linux__
    dispatcher_ = dispatcher;
    if(dispatcher_ && timer_.fd() >= 0){
        dispatcher_->subscribe(timer_.fd(), [this](){ timer_.onTick(); });
    }
#endif
}

void Scheduler::stop(){
#ifdef __linux__
    if(dispatcher_ && timer_.fd() >= 0){
        dispatcher_->unsubscribe(timer_.fd());
    }
#endif
    timer_.stop();
}
