#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include "core/actor_system/actor/actor_handle.hpp"

class Actor;

class IActorRegistry{
public:
    virtual ~IActorRegistry() = default;

    virtual ActorHandle findByName(const std::string& name) = 0;
    virtual ActorHandle findById(uint64_t id) = 0;
    virtual Actor* resolve(const ActorHandle& handle) const = 0;

    virtual void forEachActor(const std::function<void(ActorHandle)>& callback) const = 0;

    virtual void add(Actor* actor) = 0;
    virtual void remove(Actor* actor) = 0;
    virtual void clear() = 0;
};
