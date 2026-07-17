#pragma once
#include "benchmark.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/common/time/time.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct LatencyParams{
    int workers = 4;
    int iterations = 100000;
    int maxbatch = 32;
    int warmup = 0;
    size_t mailbox = 0;

    static LatencyParams parse(const IBenchmark::Args& args);
};

class LatencyBenchActor : public Actor{
public:
    LatencyBenchActor(const std::string& name, uint64_t id,
                      std::vector<uint64_t>& latencies,
                      std::vector<int64_t>& sendTimestamps,
                      std::atomic<uint64_t>& ackCounter)
        : Actor(name, id),
          latencies_(latencies),
          sendTimestamps_(sendTimestamps),
          ackCounter_(ackCounter){}

    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message& /*msg*/) override {
        uint64_t idx = ackCounter_.load(std::memory_order_relaxed);
        uint64_t receiveTime = Time::nowNs();
        int64_t sendTime = sendTimestamps_[idx];
        latencies_[idx] = receiveTime - sendTime;
        ackCounter_.fetch_add(1, std::memory_order_release);
    }

    size_t mailboxCapacity() const { return actorContext()->mailboxCapacity(); }
    uint64_t processed() const { return ackCounter_.load(std::memory_order_relaxed); }

private:
    std::vector<uint64_t>& latencies_;
    std::vector<int64_t>& sendTimestamps_;
    std::atomic<uint64_t>& ackCounter_;
};

class LatencyBenchmark : public IBenchmark{
public:
    const char* name() const override { return "latency"; }
    const char* description() const override { return "Measure per-message end-to-end latency"; }
    Result run(const Args& args) override;
};
