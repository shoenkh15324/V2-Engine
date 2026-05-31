#pragma once
#include <cstdint>
#include <string>
#include "message.hpp"

class ActorContext;

class Actor{
    friend class ActorContext;
public:
    explicit Actor(std::string name = "unknown", uint64_t id = -1);
    virtual ~Actor() = default;
    virtual void handle(const Message& msg) = 0;
    void sendMsg(Actor* target, MessagePtr msg);
    const std::string& name() const { return name_; }
    uint64_t id() const { return id_; }

private:
    ActorContext* context_ = nullptr;
    std::string name_;
    uint64_t id_;
};
