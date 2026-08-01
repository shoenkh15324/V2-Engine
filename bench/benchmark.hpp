#pragma once
#include "i_benchmark.hpp"
#include "bench_throughput.hpp"
#include "bench_latency.hpp"
#include "bench_contention.hpp"
#include "bench_scaling.hpp"
#include "bench_backpressure.hpp"
#include "bench_scheduler.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

struct BenchmarkConfig{
    int workers{0};
    int actors{0};
    int maxBatch{0};
    size_t mailboxSize{0};
    int warmup{0};
};

struct BenchmarkResult{
    std::string benchmarkName;
    std::string description;

    bool success{true};
    std::string errorMsg;

    BenchmarkConfig config{};
    std::vector<ActorSnap> actorSnaps;

    ThroughputResult throughput{};
    LatencyResult latency{};
    BackpressureResult backpressure{};
    ScalingResult scaling{};
    SchedulerResult scheduler{};
};

using BenchmarkArgs = IBenchmark::Args;
using BenchmarkCreator = std::function<std::unique_ptr<IBenchmark>()>;

struct BenchmarkInfo{
    std::string name;
    std::string description;
};

class Benchmark{
public:
    Benchmark() = delete;

    Benchmark(const Benchmark&) = delete;
    Benchmark& operator=(const Benchmark&) = delete;
    Benchmark(Benchmark&&) = delete;
    Benchmark& operator=(Benchmark&&) = delete;

    static void registerBenchmark(const std::string& name, BenchmarkCreator creator);
    static std::vector<BenchmarkInfo> list();
    static BenchmarkResult run(const std::string& name, const BenchmarkArgs& args);
    static std::string runAll(const BenchmarkArgs& args);

private:
    struct RegistryEntry{
        std::string name;
        std::string description;
        BenchmarkCreator creator;
    };
    static std::vector<RegistryEntry>& entries();
};
