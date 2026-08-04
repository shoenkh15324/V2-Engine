#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

struct ActorMetricsSnapshot {
    uint64_t id;
    uint64_t enqueued;
    uint64_t processed;
    uint64_t dropped;
    uint64_t handleTimeNs;
    uint64_t batches;
    size_t peakDepth;
    std::string name;
};

struct WorkerMetricsSnapshot {
    uint64_t batches;
    uint64_t busyTimeNs;
    uint64_t idleTimeNs;
    uint64_t messages;
};

struct DispatcherMetricsSnapshot {
    uint64_t dispatchCount;
    uint64_t acquireCount;
    size_t readyQueuePeak;
};

struct MetricsSnapshot {
    std::vector<ActorMetricsSnapshot> actors;
    std::vector<WorkerMetricsSnapshot> workers;
    DispatcherMetricsSnapshot dispatcher;
};

class IMetrics {
public:
    virtual ~IMetrics() = default;
    
    virtual void init(size_t numWorkers) = 0;
    virtual void registerActor(uint64_t actorId) = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;
    virtual void recordEnqueue(uint64_t actorId, bool success, size_t depth) = 0;
    virtual void recordHandle(uint64_t actorId, size_t count, uint64_t durationNs) = 0;
    virtual void recordBatch(int workerId, size_t msgCount, uint64_t busyNs, uint64_t idleNs) = 0;
    virtual void recordDispatch(size_t queueDepth) = 0;
    virtual void recordAcquire() = 0;
    virtual MetricsSnapshot snapshot() = 0;
    virtual void reset() = 0;
};
