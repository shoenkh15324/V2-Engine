#include "actor.hpp"
#include "core/common/util/return.hpp"
#include "core/common/log/log.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/i_scheduler.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"

Actor::Actor(std::string name, uint64_t id) : name_(name), id_(id){
    //
}

Actor::~Actor(){
    if(!runtime_ || !runtime_->scheduler()) return;
    for(int id : timerIds_){
        runtime_->scheduler()->cancel(id);
    }
    timerIds_.clear();
}

void Actor::sendMsg(const std::string& targetName, Message msg){
    auto* target = runtime_->actorRegistry()->findByName(targetName);
    if(target){
        target->receiveMsg(std::move(msg));
    }
}

void Actor::sendMsg(uint64_t targetId, Message msg){
    auto* target = runtime_->actorRegistry()->findById(targetId);
    if(target){
        target->receiveMsg(std::move(msg));
    }
}

int Actor::sendMsgAfter(const std::string& targetName, Message msg, uint64_t delayMs){
    auto* target = runtime_->actorRegistry()->findByName(targetName);
    if(!target){
        return Fail;
    }
    return runtime_->scheduler() ? runtime_->scheduler()->addTimer(target, std::move(msg), delayMs, false) : Fail;
}

int Actor::sendMsgAfter(uint64_t targetId, Message msg, uint64_t delayMs){
    auto* target = runtime_->actorRegistry()->findById(targetId);
    if(!target){
        return Fail;
    }
    return runtime_->scheduler() ? runtime_->scheduler()->addTimer(target, std::move(msg), delayMs, false) : Fail;
}

void Actor::receiveMsg(Message msg){
    runtime_->enqueue(std::move(msg));
}

int Actor::startTimer(Message msg, uint64_t delayMs, bool repeating){
    auto* scheduler = runtime_->scheduler();
    if(!scheduler) return Fail;
    int id = scheduler->addTimer(this, std::move(msg), delayMs, repeating);
    if(id != Fail) timerIds_.insert(id);
    return id;
}

void Actor::cancelTimer(int timerId){
    if(!runtime_->scheduler()) return;
    runtime_->scheduler()->cancel(timerId);
    timerIds_.erase(timerId);
}

size_t Actor::mailboxCount() const {
    return runtime_ ? runtime_->mailboxCount() : 0;
}

size_t Actor::mailboxCapacity() const {
    return runtime_ ? runtime_->mailboxCapacity() : 0;
}

