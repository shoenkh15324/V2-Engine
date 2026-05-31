#include "actor.hpp"
#include "core/actor_system/runtime/actor_context.hpp"

void Actor::sendMsg(Actor* target, MessagePtr msg){
    target->context_->enqueue(std::move(msg));
}
