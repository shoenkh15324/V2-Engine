#pragma once
#include <stdint.h>
#include "core/actor_system/messages/message.hpp"

class IActorRegistry;
class Actor;

class ActorHandle{
public:
    ActorHandle() : id_(0), generation_(0), registry_(nullptr){}
    ActorHandle(uint64_t id, uint64_t generation, const IActorRegistry* reg) : id_(id), generation_(generation), registry_(reg){}

    bool operator==(const ActorHandle& other) const;
    bool operator!=(const ActorHandle& other) const;

    bool valid() const;
    Actor* get() const;
    void send(Message msg) const;

    uint64_t id() const { return id_; }
    uint64_t generation() const { return generation_; }

private:
    uint64_t id_;
    uint64_t generation_;
    const IActorRegistry* registry_;
};
