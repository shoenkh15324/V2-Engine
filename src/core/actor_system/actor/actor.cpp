#include "actor.hpp"
#include "core/actor_system/runtime/actor_context.hpp"

namespace core::actor{

void Actor::sendMsg(Actor* target, MessagePtr msg){
    target->context_->enqueue(std::move(msg));
};

} // namespace core::actor
