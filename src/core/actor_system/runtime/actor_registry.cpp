#include "actor_registry.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_handle.hpp"
#include "core/common/log/log.hpp"

ActorHandle ActorRegistry::findByName(const std::string& name){
    std::lock_guard lock(mutex_);
    auto it = byName_.find(name);
    if(it == byName_.end()) return ActorHandle();
    return ActorHandle(it->second.actor->id(), it->second.generation, this);
}

ActorHandle ActorRegistry::findById(uint64_t id){
    std::lock_guard lock(mutex_);
    auto it = byId_.find(id);
    if(it == byId_.end()) return ActorHandle();
    return ActorHandle(id, it->second.generation, this);
}

Actor* ActorRegistry::resolve(const ActorHandle& handle) const {
    std::lock_guard lock(mutex_);
    auto it = byId_.find(handle.id());
    if(it == byId_.end()) return nullptr;
    if(it->second.generation != handle.generation()) return nullptr;
    return it->second.actor;
}

void ActorRegistry::forEachActor(const std::function<void(ActorHandle)>& callback) const {
    std::vector<ActorHandle> snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot.reserve(byId_.size());
        for(auto& [id, entry] : byId_){
            snapshot.emplace_back(id, entry.generation, this);
        }
    }
    for(const auto& handle : snapshot){
        callback(handle);
    }
}

void ActorRegistry::add(Actor* actor){
    std::lock_guard lock(mutex_);
    uint64_t id = actor->id();
    uint64_t gen = generations_[id]++;
    auto nameIt = byName_.find(actor->name());
    if(nameIt != byName_.end() && (nameIt->second.actor != actor)){
        V2_LOG_ERROR("Actor name '{}' already registered by id {}, overwriting with id {}",
                     actor->name().c_str(), nameIt->second.actor->id(), id);
    }
    byName_[actor->name()] = {actor, gen};
    byId_[id] = {actor, gen};
    actor->setGeneration(gen);
}

void ActorRegistry::remove(Actor* actor){
    std::lock_guard lock(mutex_);
    uint64_t id = actor->id();
    generations_[id]++;
    auto nameIt = byName_.find(actor->name());
    if((nameIt != byName_.end()) && (nameIt->second.actor == actor)){
        byName_.erase(nameIt); // 다른 액터가 같은 이름을 재사용했으면 건드리지 않음
    }
    byId_.erase(id);
}

void ActorRegistry::clear(){
    std::lock_guard lock(mutex_);
    byName_.clear();
    byId_.clear();
}
