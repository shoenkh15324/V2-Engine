#pragma once
#include <atomic>
#include <vector>
#include <memory>
#include <semaphore>
#include "core/common/container/lock_free_mpsc_queue.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"

class WorkDispatcher : public IWorkDispatcher {
public:
    explicit WorkDispatcher(int workerCount);
    ~WorkDispatcher();

    WorkDispatcher(const WorkDispatcher&) = delete;
    WorkDispatcher& operator=(const WorkDispatcher&) = delete;
    WorkDispatcher(WorkDispatcher&&) = delete;
    WorkDispatcher& operator=(WorkDispatcher&&) = delete;

    void start();
    void stop();
    void dispatch(ActorRuntime* actorRuntime) override;
    ActorRuntime* acquire(int workerId) override;
    bool isRunning() const override { return running_; }

private:
    int workerCount_ = 0;
    std::atomic<bool> running_{false};
    std::vector<std::unique_ptr<std::counting_semaphore<>>> semas_;
    std::vector<std::unique_ptr<LockFreeMpscQueue<ActorRuntime*>>> queues_;
};
