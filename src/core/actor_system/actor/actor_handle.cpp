#include "actor_handle.hpp"
#include "core/actor_system/runtime/i_actor_registry.hpp"
#include "core/actor_system/actor/actor.hpp"

bool ActorHandle::operator==(const ActorHandle& other) const {
    return ((id_ == other.id_) && (generation_ == other.generation_));
}

bool ActorHandle::operator!=(const ActorHandle& other) const {
    return !(*this == other);
}

bool ActorHandle::valid() const {
    if(!registry_) return false;
    return registry_->resolve(*this) != nullptr;
}

Actor* ActorHandle::get() const {
    if(!registry_) return nullptr;
    return registry_->resolve(*this);
}

void ActorHandle::send(Message msg) const {
    Actor* actor = get();
    if(actor){
        actor->receiveMsg(std::move(msg));
    }
}
