#pragma once
#include <atomic>
#include <vector>
#include <memory>
#include <semaphore>
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"

class WorkDispatcher : public IWorkDispatcher {
public:
    explicit WorkDispatcher(int workerCount, int highWatermark = (kQueueCapacity * 7 / 10));
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
    static constexpr int kQueueCapacity = 1024;

    bool enqueueEntry(ActorRuntime* actorRuntime);
    int pickWorker(uint64_t actorId);
    int pickLeastLoaded(uint64_t actorId);

    int workerCount_ = 0;
    int highWatermark_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> draining_{false};
    std::atomic<size_t> pendingWork_{0};
    std::vector<std::unique_ptr<std::counting_semaphore<>>> semas_;
    std::vector<std::unique_ptr<LockFreeMpscQueue<ActorRuntime*>>> queues_;
};
