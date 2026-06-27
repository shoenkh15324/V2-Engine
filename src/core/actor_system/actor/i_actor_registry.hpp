#pragma once
#include <cstdint>
#include <string>
#include <functional>

class Actor;

class IActorRegistry{
public:
    virtual ~IActorRegistry() = default;
    virtual Actor* findByName(const std::string& name) const = 0;
    virtual Actor* findById(uint64_t id) const = 0;
    virtual void forEachActor(const std::function<void(Actor*)>& callback) const = 0;
    virtual void remove(Actor* actor) = 0;
};
