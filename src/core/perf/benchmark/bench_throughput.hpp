#pragma once
#include "benchmark.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include <atomic>
#include <cstdint>
#include <string>

class BenchActor : public Actor{
public:
    BenchActor(const std::string& name, uint64_t id, std::atomic<uint64_t>& counter)
        : Actor(std::move(name), id), counter_(counter), actorName_(name){}

    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message& msg) override {
        counter_.fetch_add(1, std::memory_order_relaxed);
        processed_.fetch_add(1, std::memory_order_relaxed);
    }

    const std::string& actorName() const { return actorName_; }
    size_t mailboxCapacity() const { return actorContext()->mailboxCapacity(); }
    uint64_t processed() const { return processed_.load(std::memory_order_relaxed); }

private:
    std::atomic<uint64_t>& counter_;
    std::atomic<uint64_t> processed_{0};
    std::string actorName_;
};

class ThroughputBenchmark : public IBenchmark{
public:
    const char* name() const override { return "throughput"; }
    const char* description() const override { return "Measure message throughput"; }
    Result run(const Args& args) override;
};
