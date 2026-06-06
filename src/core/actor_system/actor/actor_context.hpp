#pragma once
#include <atomic>
#include <memory>
#include "core/actor_system/messages/message.hpp"
#include "core/actor_system/runtime/mailbox.hpp"
#include "core/actor_system/actor/actor.hpp"

class Dispatcher;
class Scheduler;
class ActorRegistry;

class ActorContext{
public:
    ActorContext(std::unique_ptr<Actor> actor, size_t mailboxSize, Dispatcher* dispatcher, Scheduler* scheduler, ActorRegistry* actorRegistry);
    ~ActorContext();
    void enqueue(Message msg);
    void run(int maxBatch);
    Actor* actor() const { return actor_.get(); }
    Scheduler* scheduler() const { return scheduler_; }
    ActorRegistry* actorRegistry() const { return actorRegistry_; }

private:
    std::unique_ptr<Actor> actor_;
    Mailbox<Message> mailbox_;
    std::atomic<bool> scheduled_{false};
    Dispatcher* dispatcher_ = nullptr;
    Scheduler* scheduler_ = nullptr;
    ActorRegistry* actorRegistry_ = nullptr;
};
