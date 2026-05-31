#include "actor.hpp"
#include "core/actor_system/runtime/actor_context.hpp"

Actor::Actor(std::string name, uint64_t id)
    : name_(name), id_(id)
{
}

void Actor::sendMsg(Actor* target, MessagePtr msg){
    target->context_->enqueue(std::move(msg));
}
