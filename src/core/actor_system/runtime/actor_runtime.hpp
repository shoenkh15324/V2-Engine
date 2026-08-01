#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/messages/message.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/actor_system/runtime/i_scheduler.hpp"
#include "core/actor_system/runtime/i_actor_registry.hpp"
#include "core/actor_system/runtime/supervisor/i_supervised.hpp"

class IWorkDispatcher;
class IEventLoop;
class ISupervisor;

class ActorRuntime : public IActorRuntime, public ISupervised {
public:
    ActorRuntime(std::unique_ptr<Actor> actor, std::unique_ptr<LockFreeMpscQueue<Message>> mailbox, IWorkDispatcher* workDispatcher, IScheduler* scheduler, IActorRegistry* actorRegistry, IEventLoop* eventLoop = nullptr, ISupervisor* supervisor = nullptr);
    ~ActorRuntime();

    ActorRuntime(const ActorRuntime&) = delete;
    ActorRuntime& operator=(const ActorRuntime&) = delete;
    ActorRuntime(ActorRuntime&&) = delete;
    ActorRuntime& operator=(ActorRuntime&&) = delete;

    void enqueue(Message msg) override;
    int run(int maxBatch, bool* moreWork = nullptr);
    Actor* actor() const override { return actor_.get(); }
    IActorRegistry* actorRegistry() const override { return actorRegistry_; }
    IEventLoop* eventLoop() const override { return eventLoop_; }
    size_t mailboxCount() const override { return mailbox_->count(); }
    size_t mailboxCapacity() const override { return mailbox_->capacity(); }

    int addTimer(Actor* target, Message msg, uint64_t delayMs, bool repeating) override;
    void cancelTimer(int timerId) override;
    void cancelAllTimers() override;
    size_t timerCount() const override;

    // ISupervised
    bool tryRestart(const std::string& reason, int maxRestarts) override;
    void shutdown() override;
    bool drainMailbox(Message& msg) override;
    int restartCount() const override;
    uint64_t actorId() const override;
    const std::string& actorName() const override;

private:
    bool handleLifecycle(const Message& msg);
    void performRestart(const std::string& reason);

    std::unique_ptr<Actor> actor_;
    std::unique_ptr<LockFreeMpscQueue<Message>> mailbox_;
    IWorkDispatcher* workDispatcher_ = nullptr;
    IScheduler* scheduler_ = nullptr;
    IActorRegistry* actorRegistry_ = nullptr;
    IEventLoop* eventLoop_ = nullptr;
    ISupervisor* supervisor_ = nullptr;
    mutable std::mutex timerMutex_;
    std::unordered_set<int> timerIds_;
    std::atomic<int> restartCount_{0};
    std::atomic<bool> stopped_{false};
};
