#include "actor.hpp"
#include "core/common/return.hpp"
#include "core/actor_system/actor/actor_context.hpp"

Actor::Actor(std::string name, uint64_t id) : name_(name), id_(id){
    //
}

Actor::~Actor(){
    if(!actorCtx_->scheduler()) return;
    for(int id : timerIds_){
        actorCtx_->scheduler()->cancel(id);
    }
    timerIds_.clear();
}

void Actor::sendMsg(const std::string& targetName, Message msg){
    auto* target = actorCtx_->actorRegistry()->findByName(targetName);
    if(target){
        target->receiveMsg(std::move(msg));
    }
}

void Actor::sendMsg(uint64_t targetId, Message msg){
    auto* target = actorCtx_->actorRegistry()->findById(targetId);
    if(target){
        target->receiveMsg(std::move(msg));
    }
}

int Actor::sendMsgAfter(const std::string& targetName, Message msg, uint64_t delayMs){
    auto* target = actorCtx_->actorRegistry()->findByName(targetName);
    if(!target){
        return Fail;
    }
    return actorCtx_->scheduler() ? actorCtx_->scheduler()->addTimer(target, std::move(msg), delayMs, false) : Fail;
}

int Actor::sendMsgAfter(uint64_t targetId, Message msg, uint64_t delayMs){
    auto* target = actorCtx_->actorRegistry()->findById(targetId);
    if(!target){
        return Fail;
    }
    return actorCtx_->scheduler() ? actorCtx_->scheduler()->addTimer(target, std::move(msg), delayMs, false) : Fail;
}

void Actor::receiveMsg(Message msg){
    actorCtx_->enqueue(std::move(msg));
}

int Actor::startTimer(Message msg, uint64_t delayMs, bool repeating){
    auto* scheduler = actorCtx_->scheduler();
    if(!scheduler) return Fail;
    int id = scheduler->addTimer(this, std::move(msg), delayMs, repeating);
    if(id != Fail) timerIds_.insert(id);
    return id;
}

void Actor::cancelTimer(int timerId){
    if(!actorCtx_->scheduler()) return;
    actorCtx_->scheduler()->cancel(timerId);
    timerIds_.erase(timerId);
}
