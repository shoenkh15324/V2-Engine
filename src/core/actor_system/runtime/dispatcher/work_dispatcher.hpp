#pragma once
#include <atomic>
#include <vector>
#include <memory>
#include <cstdint>
#include <semaphore>
#include "core/common/container/cache_line.hpp"
#include "core/common/container/lock_free_mpmc_queue.hpp"
#include "core/actor_system/runtime/dispatcher/i_work_dispatcher.hpp"

inline constexpr int kDefaultDispatcherQueueCapacity = 1024;
inline constexpr int kDefaultParkSpinNs   = 3000; // 파킹 전 스핀 예산
inline constexpr int kDefaultTokenGraceNs = 5000; // 토큰 유예 예산

struct WorkDispatcherConfig{
    int workerCount;                                      // 필수 — 0/음수는 ctor에서 검증
    int queueCapacity       = kDefaultDispatcherQueueCapacity;
    int highWatermark       = 0;                          // 0이면 capacity*7/10 자동 계산
    int busyStealIntervalUs = 200;
    int idleStealIntervalUs = 2000;
    int parkSpinNs          = kDefaultParkSpinNs;
    int tokenGraceNs        = kDefaultTokenGraceNs;
};

class WorkDispatcher : public IWorkDispatcher {
public:
    static constexpr int kSpinPauseStride = 32; // 조건 재확인 주기(pause 횟수)
    static constexpr size_t kMaxActors = 1024;

    explicit WorkDispatcher(const WorkDispatcherConfig& cfg);
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
    bool settleToken(ActorRuntime* actorRuntime) override;
    void drainPendedActor();

private:
    struct alignas(kCacheLine) InFlightSlot {
        std::atomic<uint8_t> held{0};
    };

    bool enqueueEntry(ActorRuntime* actorRuntime);
    int pickWorker(uint64_t actorId);
    int pickLeastLoaded(uint64_t actorId);
    bool tryAcquireOwn(int workerId, ActorRuntime*& out);
    bool trySteal(int workerId, ActorRuntime*& out);

    InFlightSlot& inFlightSlot(uint64_t actorId);
    void releaseInFlight(uint64_t actorId);
    bool claimInFlight(uint64_t actorId);
    bool spinAcquire(int workerId, ActorRuntime*& out);
    void graceSpin(const ActorRuntime* actorRuntime);
    void retirePendingWork();

    int workerCount_ = 0;
    int queueCapacity_ = kDefaultDispatcherQueueCapacity;
    int highWatermark_ = 0;
    int parkSpinNs_ = 0;
    int tokenGraceNs_ = 0;
    int busyStealIntervalUs_ = 0;
    int idleStealIntervalUs_ = 0;
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> draining_{false};
    std::atomic<size_t> pendingWork_{0};
    std::vector<std::uint8_t> idleBackoff_; // worker-confined
    std::vector<InFlightSlot> inFlightSlots_{kMaxActors};
    std::vector<ActorRuntime*> pendedActorList_;
    std::vector<std::unique_ptr<std::counting_semaphore<>>> semas_;
    std::vector<std::unique_ptr<LockFreeMpmcQueue<ActorRuntime*>>> queues_;
};
