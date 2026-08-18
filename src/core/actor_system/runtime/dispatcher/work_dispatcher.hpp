#pragma once
#include <atomic>
#include <vector>
#include <memory>
#include <cstdint>
#include <semaphore>
#include "core/common/container/lock_free_mpmc_queue.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"

class WorkDispatcher : public IWorkDispatcher {
public:
    static constexpr int kDefaultQueueCapacity = 1024;
    static constexpr int kDefaultHighWatermark = kDefaultQueueCapacity * 7 / 10;

    explicit WorkDispatcher(
        int workerCount,
        int queueCapacity = kDefaultQueueCapacity,
        int highWatermark = kDefaultHighWatermark,
        int busyStealIntervalUs = 200,
        int idleStealIntervalUs = 2000
    );
    ~WorkDispatcher();

    WorkDispatcher(const WorkDispatcher&) = delete;
    WorkDispatcher& operator=(const WorkDispatcher&) = delete;
    WorkDispatcher(WorkDispatcher&&) = delete;
    WorkDispatcher& operator=(WorkDispatcher&&) = delete;

    void start() override;
    void stop() override;
    bool dispatch(ActorRuntime* actorRuntime) override;
    bool redispatch(ActorRuntime* actorRuntime) override;
    ActorRuntime* acquire(int workerId) override;
    bool isRunning() const override { return running_.load(std::memory_order_relaxed); }

    // drain
    void beginDrain() override;
    bool isDraining() const override { return draining_.load(std::memory_order_relaxed); }
    size_t pendingWork() const override { return pendingWork_.load(std::memory_order_relaxed); }
    void onWorkDone() override;

private:
    bool enqueueEntry(ActorRuntime* actorRuntime);
    int pickWorker(uint64_t actorId);
    int pickLeastLoaded(uint64_t actorId);
    bool tryAcquireOwn(int workerId, ActorRuntime*& out);
    bool trySteal(int workerId, ActorRuntime*& out);

    int workerCount_ = 0;
    int queueCapacity_ = kDefaultQueueCapacity;
    int highWatermark_ = 0;
    int busyStealIntervalUs_ = 0;
    int idleStealIntervalUs_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> draining_{false};
    std::atomic<size_t> pendingWork_{0};
    std::vector<std::uint8_t> idleBackoff_; // worker-confined
    std::vector<std::unique_ptr<std::counting_semaphore<>>> semas_;
    std::vector<std::unique_ptr<LockFreeMpmcQueue<ActorRuntime*>>> queues_;
};
