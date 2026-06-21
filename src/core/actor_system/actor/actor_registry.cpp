#include "actor_registry.hpp"
#include "core/actor_system/actor/actor.hpp"

void ActorRegistry::add(Actor* actor){
    std::lock_guard<std::mutex> lock(mutex_);
    byName_[actor->name()] = actor;
    byId_[actor->id()] = actor;
}

void ActorRegistry::remove(Actor* actor){
    std::lock_guard<std::mutex> lock(mutex_);
    byName_.erase(actor->name());
    byId_.erase(actor->id());
}

void ActorRegistry::clear(){
    std::lock_guard<std::mutex> lock(mutex_);
    byName_.clear();
    byId_.clear();
}

Actor* ActorRegistry::findByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byName_.find(name);
    return (it != byName_.end()) ? it->second : nullptr;
}

Actor* ActorRegistry::findById(uint64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byId_.find(id);
    return (it != byId_.end()) ? it->second : nullptr;
}
