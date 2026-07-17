#pragma once
#include "i_benchmark.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include <atomic>
#include <cstdint>
#include <string>

struct ThroughputResult{
    uint64_t iterations{0};
    uint64_t totalDurationNs{0};
    double msgsPerSec{0.0};
};

struct ThroughputParams{
    int workers = 4;
    int actors = 1;
    int iterations = 10000;
    int maxbatch = 32;
    int warmup = 0;
    size_t mailbox = 0;

    static ThroughputParams parse(const IBenchmark::Args& args);
};

class BenchActor : public Actor{
public:
    BenchActor(const std::string& name, uint64_t id, std::atomic<uint64_t>& counter)
        : Actor(name, id), counter_(counter){}

    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message& /*msg*/) override {
        counter_.fetch_add(1, std::memory_order_relaxed);
        processed_.fetch_add(1, std::memory_order_relaxed);
    }

    size_t mailboxCapacity() const { return actorContext()->mailboxCapacity(); }
    uint64_t processed() const { return processed_.load(std::memory_order_relaxed); }

private:
    std::atomic<uint64_t>& counter_;
    std::atomic<uint64_t> processed_{0};
};

class ThroughputBenchmark : public IBenchmark{
public:
    const char* name() const override { return "throughput"; }
    const char* description() const override { return "Measure message throughput"; }
    BenchmarkResult run(const Args& args) override;
};
