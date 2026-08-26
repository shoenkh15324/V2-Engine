#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include "core/actor_system/messages/message.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"
#include "core/actor_system/runtime/mailbox/i_mailbox.hpp"
#include "core/actor_system/runtime/scheduler/i_scheduler.hpp"
#include "core/actor_system/runtime/supervisor/i_supervised.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"

class IWorkDispatcher;
class IEventLoop;
class ISupervisor;

struct ActorRuntimeDeps {
    IWorkDispatcher* workDispatcher = nullptr;
    IScheduler* scheduler = nullptr;
    IActorRegistry* actorRegistry = nullptr;
    IEventLoop* eventLoop = nullptr;
    ISupervisor* supervisor = nullptr;
};

class ActorRuntime : public IActorRuntime, public ISupervised {
public:
    ActorRuntime(std::unique_ptr<Actor> actor, std::unique_ptr<IMailbox> mailbox, const ActorRuntimeDeps& deps);
    ~ActorRuntime();

    ActorRuntime(const ActorRuntime&) = delete;
    ActorRuntime& operator=(const ActorRuntime&) = delete;
    ActorRuntime(ActorRuntime&&) = delete;
    ActorRuntime& operator=(ActorRuntime&&) = delete;

    void enqueue(Message msg) override;
    int run(int maxBatch, bool* hasMoreWork = nullptr);
    Actor* actor() const override { return actor_.get(); }
    IActorRegistry* actorRegistry() const override { return actorRegistry_; }
    IEventLoop* eventLoop() const override { return eventLoop_; }
    size_t mailboxCount() const override { return mailbox_->count(); }
    size_t mailboxCapacity() const override { return mailbox_->capacity(); }
    bool isStopped() const { return stopped_.load(std::memory_order_relaxed); }

    int addTimer(Actor* target, Message msg, uint64_t delayMs, bool repeating) override;
    void cancelTimer(int timerId) override;
    void cancelAllTimers() override;
    size_t timerCount() const override;

    // ISupervised
    bool tryRestart(const std::string& reason, int maxRestarts) override;
    void shutdown() override;
    bool popDeadLetter(Message& msg) override;
    int restartCount() const override;
    uint64_t actorId() const override;
    const std::string& actorName() const override;

private:
    struct BatchResult {
        int processed = 0;
        bool hasMoreWork = false;
    };

    bool tryConsumeLifecycle(const Message& msg);
    void performRestart(const std::string& reason);
    BatchResult processBatch(int maxBatch);

    std::unique_ptr<Actor> actor_;
    std::unique_ptr<IMailbox> mailbox_;
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
