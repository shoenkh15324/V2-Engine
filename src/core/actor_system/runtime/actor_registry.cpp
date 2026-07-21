#include "actor_registry.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_handle.hpp"

ActorRegistry::ActorRegistry(){
    self_ = this;
}

ActorHandle ActorRegistry::findByName(const std::string& name){
    auto it = byName_.find(name);
    if(it == byName_.end()) return ActorHandle();
    return ActorHandle(it->second.actor->id(), it->second.generation, self_);
}

ActorHandle ActorRegistry::findById(uint64_t id){
    auto it = byId_.find(id);
    if(it == byId_.end()) return ActorHandle();
    return ActorHandle(id, it->second.generation, self_);
}

Actor* ActorRegistry::resolve(const ActorHandle& handle) const {
    auto it = byId_.find(handle.id());
    if(it == byId_.end()) return nullptr;
    if(it->second.generation != handle.generation()) return nullptr;
    return it->second.actor;
}

void ActorRegistry::forEachActor(const std::function<void(ActorHandle)>& callback) const {
    for(auto& [id, entry] : byId_){
        callback(ActorHandle(id, entry.generation, self_));
    }
}

void ActorRegistry::add(Actor* actor){
    uint64_t id = actor->id();
    uint64_t gen = generations_[id]++;
    byName_[actor->name()] = {actor, gen};
    byId_[id] = {actor, gen};
    actor->setGeneration(gen);
}

void ActorRegistry::remove(Actor* actor){
    uint64_t id = actor->id();
    generations_[id]++;
    byName_.erase(actor->name());
    byId_.erase(id);
}

void ActorRegistry::clear(){
    byName_.clear();
    byId_.clear();
}
