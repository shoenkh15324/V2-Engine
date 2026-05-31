#pragma once
#include <atomic>
#include <memory>
#include "mailbox.hpp"
#include "core/actor_system/actor/actor.hpp"

class Dispatcher;

class ActorContext{
public:
    ActorContext(std::unique_ptr<Actor> actor, size_t mailboxSize);
    void enqueue(MessagePtr msg);
    void run(int throughput);
    bool hasMessage() const;
    Actor* actor() const { return actor_.get(); }
    void attachDispatcher(Dispatcher* dispatcher) { dispatcher_ = dispatcher; }

private:
    std::unique_ptr<Actor> actor_;
    Mailbox<MessagePtr> mailbox_;
    std::atomic<bool> scheduled_{false};
    Dispatcher* dispatcher_ = nullptr;
};
