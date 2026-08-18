#include "actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"
#include "core/actor_system/runtime/scheduler/i_scheduler.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"

Actor::Actor(std::string name, uint64_t id) : name_(std::move(name)), id_(id){}

Actor::~Actor(){}

void Actor::sendMsg(const std::string& targetName, Message msg){
    if(Actor* a = runtime_->actorRegistry()->findActorByName(targetName)){
        a->receiveMsg(std::move(msg));
    }else{
        V2_LOG_WARN("Actor {} target '{}' not found, dropping message id={}", name_.c_str(), targetName.c_str(), static_cast<int>(msg.id()));
    }
}

void Actor::sendMsg(uint64_t targetId, Message msg){
    if(Actor* a = runtime_->actorRegistry()->findActorById(targetId)){
        a->receiveMsg(std::move(msg));
    }else{
        V2_LOG_WARN("Actor {} target id={} not found, dropping message id={}", name_.c_str(), targetId, static_cast<int>(msg.id()));
    }
}

int Actor::sendMsgAfter(const std::string& targetName, Message msg, uint64_t delayMs){
    Actor* a = runtime_->actorRegistry()->findActorByName(targetName);
    if(!a){
        return Fail;
    }
    return runtime_->addTimer(a, std::move(msg), delayMs, false);
}

int Actor::sendMsgAfter(uint64_t targetId, Message msg, uint64_t delayMs){
    Actor* a = runtime_->actorRegistry()->findActorById(targetId);
    if(!a){
        return Fail;
    }
    return runtime_->addTimer(a, std::move(msg), delayMs, false);
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

void Actor::handleUnknown(const Message& msg){
    V2_LOG_WARN("Actor {}: unhandled message id {}", name_.c_str(), (int)msg.id());
    V2_METRICS()->recordDeadLetter(id());
}
