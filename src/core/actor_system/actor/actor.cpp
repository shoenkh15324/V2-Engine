#include "actor.hpp"
#include "core/common/return.hpp"
#include "core/actor_system/actor/actor_context.hpp"

Actor::Actor(std::string name, uint64_t id)
    : name_(name), id_(id)
{
}

void Actor::sendMsg(const std::string& targetName, Message msg){
    auto* target = context_->actorRegistry()->findByName(targetName);
    if(target){
        target->receiveMsg(std::move(msg));
    }
}

void Actor::sendMsg(uint64_t targetId, Message msg){
    auto* target = context_->actorRegistry()->findById(targetId);
    if(target){
        target->receiveMsg(std::move(msg));
    }
}

int Actor::sendAfter(const std::string& targetName, Message msg, uint64_t delayMs){
    auto* target = context_->actorRegistry()->findByName(targetName);
    if(!target){
        return Fail;
    }
    return context_->scheduler() ? context_->scheduler()->addTimer(target, std::move(msg), delayMs, false) : Fail;
}

int Actor::sendAfter(uint64_t targetId, Message msg, uint64_t delayMs){
    auto* target = context_->actorRegistry()->findById(targetId);
    if(!target){
        return Fail;
    }
    return context_->scheduler() ? context_->scheduler()->addTimer(target, std::move(msg), delayMs, false) : Fail;
}

void Actor::receiveMsg(Message msg){
    context_->enqueue(std::move(msg));
}

int Actor::startTimer(Message msg, uint64_t delayMs, bool repeating){
    auto* sched = context_->scheduler();
    if(!sched) return Fail;
    int id = sched->addTimer(this, std::move(msg), delayMs, repeating);
    if(id != Fail) timerIds_.insert(id);
    return id;
}

void Actor::cancelTimer(int timerId){
    if(!context_->scheduler()) return;
    context_->scheduler()->cancel(timerId);
    timerIds_.erase(timerId);
}

Actor::~Actor(){
    if(!context_->scheduler()) return;
    for(int id : timerIds_) context_->scheduler()->cancel(id);
    timerIds_.clear();
}