#include "actor.hpp"
#include "core/common/util/return.hpp"
#include "core/common/log/log.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/i_scheduler.hpp"
#include "core/actor_system/runtime/i_actor_registry.hpp"
#include "core/actor_system/actor/actor_handle.hpp"

Actor::Actor(std::string name, uint64_t id) : name_(name), id_(id){
    //
}

Actor::~Actor(){
    // Timer cleanup is now handled by ActorRuntime
}

void Actor::sendMsg(const std::string& targetName, Message msg){
    ActorHandle target = runtime_->actorRegistry()->findByName(targetName);
    if(target.valid()){
        target.send(std::move(msg));
    }
}

void Actor::sendMsg(uint64_t targetId, Message msg){
    ActorHandle target = runtime_->actorRegistry()->findById(targetId);
    if(target.valid()){
        target.send(std::move(msg));
    }
}

int Actor::sendMsgAfter(const std::string& targetName, Message msg, uint64_t delayMs){
    ActorHandle target = runtime_->actorRegistry()->findByName(targetName);
    if(!target.valid()){
        return Fail;
    }
    return runtime_->addTimer(target.get(), std::move(msg), delayMs, false);
}

int Actor::sendMsgAfter(uint64_t targetId, Message msg, uint64_t delayMs){
    ActorHandle target = runtime_->actorRegistry()->findById(targetId);
    if(!target.valid()){
        return Fail;
    }
    return runtime_->addTimer(target.get(), std::move(msg), delayMs, false);
}

void Actor::receiveMsg(Message msg){
    runtime_->enqueue(std::move(msg));
}

int Actor::startTimer(Message msg, uint64_t delayMs, bool repeating){
    return runtime_ ? runtime_->addTimer(this, std::move(msg), delayMs, repeating) : Fail;
}

void Actor::cancelTimer(int timerId){
    if(runtime_) runtime_->cancelTimer(timerId);
}

void Actor::cancelAllTimers(){
    if(runtime_) runtime_->cancelAllTimers();
}

size_t Actor::timerCount() const {
    return runtime_ ? runtime_->timerCount() : 0;
}

size_t Actor::mailboxCount() const {
    return runtime_ ? runtime_->mailboxCount() : 0;
}

size_t Actor::mailboxCapacity() const {
    return runtime_ ? runtime_->mailboxCapacity() : 0;
}
