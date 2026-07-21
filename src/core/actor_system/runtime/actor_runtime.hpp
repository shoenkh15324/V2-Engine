#pragma once
#include <atomic>
#include <memory>
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/messages/message.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/i_scheduler.hpp"
#include "core/actor_system/runtime/i_actor_registry.hpp"

class Dispatcher;

class ActorContext : public IActorRuntime {
public:
    ActorContext(std::unique_ptr<Actor> actor, std::unique_ptr<LockFreeMpscQueue<Message>> mailbox, Dispatcher* dispatcher, IScheduler* scheduler, IActorRegistry* actorRegistry);
    ~ActorContext();

    ActorContext(const ActorContext&) = delete;
    ActorContext& operator=(const ActorContext&) = delete;
    ActorContext(ActorContext&&) = delete;
    ActorContext& operator=(ActorContext&&) = delete;

    void enqueue(Message msg) override;
    int run(int maxBatch);
    Actor* actor() const override { return actor_.get(); }
    IScheduler* scheduler() const override { return scheduler_; }
    IActorRegistry* actorRegistry() const override { return actorRegistry_; }
    Dispatcher* dispatcher() const override { return dispatcher_; }
    size_t mailboxCount() const override;
    size_t mailboxCapacity() const override;

private:
    std::unique_ptr<Actor> actor_;
    std::unique_ptr<LockFreeMpscQueue<Message>> mailbox_;
    std::atomic<bool> scheduled_{false};
    Dispatcher* dispatcher_ = nullptr;
    IScheduler* scheduler_ = nullptr;
    IActorRegistry* actorRegistry_ = nullptr;
};
