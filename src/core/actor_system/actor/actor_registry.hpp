#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class Actor;

class ActorRegistry{
public:
    void add(Actor* actor);
    void remove(Actor* actor);
    void clear();
    Actor* findByName(const std::string& name) const;
    Actor* findById(uint64_t id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Actor*> byName_;
    std::unordered_map<uint64_t, Actor*> byId_;
};
