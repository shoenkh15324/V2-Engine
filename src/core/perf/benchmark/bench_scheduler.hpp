#pragma once
#include "i_benchmark.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/common/time/time.hpp"
#include <mutex>
#include <string>
#include <vector>

struct SchedulerResult{
    uint64_t iterations{0};
    uint64_t totalDurationNs{0};
    double avgIntervalNs{0.0};
    double minIntervalNs{0.0};
    double maxIntervalNs{0.0};
    double p50{0.0};
    double p95{0.0};
    double p99{0.0};
    double p999{0.0};
};

struct SchedulerParams{
    int workers = 4;
    int maxbatch = 32;
    int warmup = 0;
    int intervalMs = 100;
    int durationMs = 5000;

    static SchedulerParams parse(const IBenchmark::Args& args);
};

class TimerBenchActor : public Actor{
public:
    TimerBenchActor(const std::string& name, uint64_t id, std::vector<uint64_t>& fireTimes)
        : Actor(name, id), fireTimes_(fireTimes){}

    int open() override { state_ = Opened; return 0; }
    int close() override { state_ = Closed; return 0; }
    void handle(const Message& /*msg*/) override {
        std::lock_guard<std::mutex> lock(mtx_);
        fireTimes_.push_back(Time::nowNs());
    }

private:
    std::vector<uint64_t>& fireTimes_;
    std::mutex mtx_;
};

class SchedulerBenchmark : public IBenchmark{
public:
    const char* name() const override { return "scheduler"; }
    const char* description() const override { return "Measure timer scheduling precision"; }
    BenchmarkResult run(const Args& args) override;
};
