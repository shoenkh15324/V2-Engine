#pragma once
#include <cstdint>
#include <string>
#include <atomic>
#include <memory>
#include <vector>
#include "core/common/container/cache_line.hpp"

struct ActorMetrics{
    alignas(kCacheLine) std::atomic<uint64_t> enqueued{0}; // Num of enqueue calls
    alignas(kCacheLine) std::atomic<uint64_t> processed{0}; // Num of handle processing
    alignas(kCacheLine) std::atomic<uint64_t> dropped{0}; // Mailbox pool drop
    alignas(kCacheLine) std::atomic<uint64_t> handleTimeNs{0}; // Handle() accumulated time
    alignas(kCacheLine) std::atomic<uint64_t> batches{0}; // Num of run() calls
    alignas(kCacheLine) std::atomic<size_t> peakDepth{0}; // Max depth of mailbox

    struct Snapshot{
        uint64_t enqueued;
        uint64_t processed;
        uint64_t dropped;
        uint64_t handleTimeNs;
        uint64_t batches;
        size_t peakDepth;
    };

    Snapshot snapshot() const {
        return{
            enqueued.load(std::memory_order_relaxed),
            processed.load(std::memory_order_relaxed),
            dropped.load(std::memory_order_relaxed),
            handleTimeNs.load(std::memory_order_relaxed),
            batches.load(std::memory_order_relaxed),
            peakDepth.load(std::memory_order_relaxed)
        };
    }

    void reset(){
        enqueued.store(0, std::memory_order_relaxed);
        processed.store(0, std::memory_order_relaxed);
        dropped.store(0, std::memory_order_relaxed);
        handleTimeNs.store(0, std::memory_order_relaxed);
        batches.store(0, std::memory_order_relaxed);
        peakDepth.store(0, std::memory_order_relaxed);
    }
};

struct WorkerMetrics{
    alignas(kCacheLine) std::atomic<uint64_t> batches{0}; // Num of cycle about acquire ->run
    alignas(kCacheLine) std::atomic<uint64_t> busyTimeNs{0}; // Time of actorCtx->run()
    alignas(kCacheLine) std::atomic<uint64_t> idleTimeNs{0}; // Wait time of semaphore acquire 
    alignas(kCacheLine) std::atomic<uint64_t> messages{0}; // Total number of processed messages

    struct Snapshot{
        uint64_t batches;
        uint64_t busyTimeNs;
        uint64_t idleTimeNs;
        uint64_t messages;
    };

    Snapshot snapshot() const{
        return{
            batches.load(std::memory_order_relaxed),
            busyTimeNs.load(std::memory_order_relaxed),
            idleTimeNs.load(std::memory_order_relaxed),
            messages.load(std::memory_order_relaxed)
        };
    }

    void reset(){
        batches.store(0, std::memory_order_relaxed);
        busyTimeNs.store(0, std::memory_order_relaxed);
        idleTimeNs.store(0, std::memory_order_relaxed);
        messages.store(0, std::memory_order_relaxed);
    }
};

struct DispatcherMetrics{
    alignas(kCacheLine) std::atomic<uint64_t> dispatchCount{0}; // Num of dispatch() calls
    alignas(kCacheLine) std::atomic<uint64_t> acquireCount{0}; // Num of acquire() calls
    alignas(kCacheLine) std::atomic<uint64_t> deduplicated{0}; // Num of duplicate rejections
    alignas(kCacheLine) std::atomic<size_t> readyQueuePeak{0}; // Max depth of ready queue

    struct Snapshot{
        uint64_t dispatchCount;
        uint64_t acquireCount;
        uint64_t deduplicated;
        size_t readyQueuePeak;
    };

    Snapshot snapshot() const{
        return{
            dispatchCount.load(std::memory_order_relaxed),
            acquireCount.load(std::memory_order_relaxed),
            deduplicated.load(std::memory_order_relaxed),
            readyQueuePeak.load(std::memory_order_relaxed)
        };
    }

    void reset(){
        dispatchCount.store(0, std::memory_order_relaxed);
        acquireCount.store(0, std::memory_order_relaxed);
        deduplicated.store(0, std::memory_order_relaxed);
        readyQueuePeak.store(0, std::memory_order_relaxed);
    }
};

class Metrics{
public:
    Metrics() = delete;

    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;
    Metrics(Metrics&&) = delete;
    Metrics& operator=(Metrics&&) = delete;

    static void init(size_t numbers);
    static void registerActor(uint64_t actorId);

    static void setEnabled(bool enabled){ enabled_ = enabled; }
    static bool isEnabled(){ return enabled_; }

    static void recordEnqueue(uint64_t actorId, bool success, size_t depth);
    static void recordHandle(uint64_t actorId, size_t count, uint64_t durationNs);
    static void recordBatch(int workerId, size_t msgCount, uint64_t busyNs, uint64_t idleNs);
    static void recordDispatch(bool deduped, size_t queueDepth);
    static void recordAcquire();

    struct Snapshot{
        struct ActorSnap{
            uint64_t id;
            std::string name = "";
            ActorMetrics::Snapshot data;
        };
        std::vector<ActorSnap> actors;
        std::vector<WorkerMetrics::Snapshot> workers;
        DispatcherMetrics::Snapshot dispatcher;
    };

    static Snapshot snapshot();
    static void reset();

private:
    static void updatePeak(std::atomic<size_t>& peak, size_t current);

    static inline bool enabled_{false};
    static inline std::vector<std::unique_ptr<ActorMetrics>> actors_;
    static inline std::vector<std::unique_ptr<WorkerMetrics>> workers_;
    static inline DispatcherMetrics dispatcher_;
};
