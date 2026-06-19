#pragma once
#include <cstdint>
#include <string>
#include "core/actor_system/messages/message.hpp"

class ActorContext;

class Actor{
    friend class ActorContext;
public:
    explicit Actor(std::string name = "unknown", uint64_t id = -1);
    virtual ~Actor() = default;
    virtual void onStart() {}
    virtual void handle(const Message& msg) = 0;
    void sendMsg(const std::string& name, Message msg);
    void sendMsg(uint64_t id, Message msg);
    int sendAfter(const std::string& targetName, Message msg, uint64_t delayMs);
    int sendAfter(uint64_t targetId, Message msg, uint64_t delayMs);
    void receiveMsg(Message msg);

    int startTimer(Message msg, uint64_t delayMs, bool repeating);
    void cancelTimer(int id);

    const std::string& name() const { return name_; }
    uint64_t id() const { return id_; }

protected:
    ActorContext* context() const { return context_; }

private:
    ActorContext* context_ = nullptr;
    std::string name_;
    uint64_t id_;
};
