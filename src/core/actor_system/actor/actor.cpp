#include "actor.hpp"
#include "core/actor_system/runtime/actor_context.hpp"
#include "core/actor_system/runtime/scheduler.hpp"

Actor::Actor(std::string name, uint64_t id)
    : name_(name), id_(id)
{
}

void Actor::sendMsg(Actor* target, Message msg){
    target->context_->enqueue(std::move(msg));
}

int Actor::sendAfter(Actor* target, Message msg, uint64_t delayMs){
    return context_->scheduler() ? context_->scheduler()->addTimer(target, std::move(msg), delayMs, false) : -1;
}

void Actor::receiveMsg(Message msg){
    context_->enqueue(std::move(msg));
}

int Actor::startTimer(Message msg, uint64_t delayMs, bool repeating){
    return context_->scheduler() ? context_->scheduler()->addTimer(this, std::move(msg), delayMs, repeating) : -1;
}

void Actor::cancelTimer(int id){
    if(context_->scheduler()){
        context_->scheduler()->cancel(id);
    }
}