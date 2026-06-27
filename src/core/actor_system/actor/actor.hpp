#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>
#include "core/actor_system/messages/message.hpp"

class ActorContext;

class Actor{
    friend class ActorContext;
public:
    explicit Actor(std::string name = "unknown", uint64_t id = -1);
    virtual ~Actor();

    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;
    Actor(Actor&&) = delete;
    Actor& operator=(Actor&&) = delete;

    virtual void onStart() {}
    virtual void handle(const Message& msg) = 0;
    
    void sendMsg(const std::string& name, Message msg);
    void sendMsg(uint64_t id, Message msg);
    int sendMsgAfter(const std::string& targetName, Message msg, uint64_t delayMs);
    int sendMsgAfter(uint64_t targetId, Message msg, uint64_t delayMs);
    void receiveMsg(Message msg);

    int startTimer(Message msg, uint64_t delayMs, bool repeating);
    void cancelTimer(int timerId);

    size_t mailboxCount() const;
    size_t mailboxCapacity() const;

    const std::string& name() const { return name_; }
    uint64_t id() const { return id_; }

protected:
    ActorContext* actorContext() const { return actorCtx_; }

private:
    ActorContext* actorCtx_ = nullptr;
    std::string name_;
    std::unordered_set<int> timerIds_;
    uint64_t id_;
};
