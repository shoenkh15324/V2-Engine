#pragma once
#include <string>
#include <cstdint>
#include <shared_mutex>
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

    ActorHandle findHandleByName(const std::string& name) override;
    ActorHandle findHandleById(uint64_t id) override;
    Actor* findActorByName(const std::string& name) override;
    Actor* findActorById(uint64_t id) override;
    Actor* resolve(const ActorHandle& handle) const override;

    void forEachActor(const std::function<void(ActorHandle)>& callback) const override;
    
    void add(Actor* actor) override;
    void remove(Actor* actor) override;
    void clear() override;

private:
    struct ActorEntry{
        Actor* actor;
        uint64_t generation;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ActorEntry> byName_;
    std::unordered_map<uint64_t, ActorEntry> byId_;
    std::unordered_map<uint64_t, uint64_t> generations_;
};
