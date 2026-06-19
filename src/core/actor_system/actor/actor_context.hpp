#pragma once
#include <atomic>
#include <memory>
#include "core/actor_system/messages/message.hpp"
#include "core/actor_system/runtime/mailbox.hpp"
#include "core/actor_system/runtime/i_scheduler.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"

class Actor;
class Dispatcher;

class ActorContext{
public:
    ActorContext(std::unique_ptr<Actor> actor, size_t mailboxSize, Dispatcher* dispatcher, IScheduler* scheduler, IActorRegistry* actorRegistry);
    ~ActorContext();
    void enqueue(Message msg);
    void run(int maxBatch);
    Actor* actor() const { return actor_.get(); }
    IScheduler* scheduler() const { return scheduler_; }
    IActorRegistry* actorRegistry() const { return actorRegistry_; }
    Dispatcher* dispatcher() const { return dispatcher_; }

private:
    std::unique_ptr<Actor> actor_;
    Mailbox<Message> mailbox_;
    std::atomic<bool> scheduled_{false};
    Dispatcher* dispatcher_ = nullptr;
    IScheduler* scheduler_ = nullptr;
    IActorRegistry* actorRegistry_ = nullptr;
};
