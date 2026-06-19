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

void Scheduler::subscribeTimerFd(){
#ifdef __linux__
    if(dispatcher_ && timer_.fd() >= 0){
        dispatcher_->subscribe(timer_.fd(), [this](){ timer_.onTick(); });
    }
#endif
}

void Scheduler::unsubscribeTimerFd(){
#ifdef __linux__
    if(dispatcher_ && timer_.fd() >= 0){
        dispatcher_->unsubscribe(timer_.fd());
    }
#endif
}

void Scheduler::start(Dispatcher* dispatcher){
    timer_.start();
    dispatcher_ = dispatcher;
    subscribeTimerFd();
}

void Scheduler::stop(){
    unsubscribeTimerFd();
    timer_.stop();
}
