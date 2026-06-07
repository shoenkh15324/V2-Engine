#pragma once
#include <cstdint>
#include <string>

class Actor;

class IActorRegistry{
public:
    virtual ~IActorRegistry() = default;
    virtual Actor* findByName(const std::string& name) const = 0;
    virtual Actor* findById(uint64_t id) const = 0;
    virtual void remove(Actor* actor) = 0;
};
