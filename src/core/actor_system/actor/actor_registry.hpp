#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include "core/actor_system/actor/i_actor_registry.hpp"

class Actor;

class ActorRegistry : public IActorRegistry{
public:
    ActorRegistry() = default;
    ~ActorRegistry() = default;
    
    ActorRegistry(const ActorRegistry&) = delete;
    ActorRegistry& operator=(const ActorRegistry&) = delete;
    ActorRegistry(ActorRegistry&&) = delete;
    ActorRegistry& operator=(ActorRegistry&&) = delete;

    void add(Actor* actor);
    void remove(Actor* actor);
    void clear();
    Actor* findByName(const std::string& name) const override;
    Actor* findById(uint64_t id) const override;
    void forEachActor(const std::function<void(Actor*)>& callback) const override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Actor*> byName_;
    std::unordered_map<uint64_t, Actor*> byId_;
};
