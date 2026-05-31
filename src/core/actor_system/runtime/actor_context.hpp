#pragma once
#include <atomic>
#include <memory>
#include "mailbox.hpp"
#include "core/actor_system/actor/actor.hpp"

namespace core::runtime{

class Dispatcher;

class ActorContext{
public:
    ActorContext(std::unique_ptr<core::actor::Actor> actor, size_t mailboxSize);
    void enqueue(core::actor::MessagePtr msg);
    void run(int throughput);
    bool hasMessage() const;
    core::actor::Actor* actor() const { return actor_.get(); }
    void attachDispatcher(Dispatcher* dispatcher) { dispatcher_ = dispatcher; }

private:
    std::unique_ptr<core::actor::Actor> actor_;
    Mailbox<core::actor::MessagePtr> mailbox_;
    std::atomic<bool> scheduled_{false};
    Dispatcher* dispatcher_ = nullptr;
};

} // namespace core::runtime
