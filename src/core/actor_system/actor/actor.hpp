#pragma once
#include <cstdint>
#include "message.hpp"

class ActorContext;

class Actor{
    friend class ActorContext;
public:
    virtual void handle(const Message& msg) = 0;
    virtual ~Actor() = default;
    void sendMsg(Actor* target, MessagePtr msg);
    uint64_t id() const { return id_; }

private:
    ActorContext* context_ = nullptr;
    uint64_t id_ = 0;
};
