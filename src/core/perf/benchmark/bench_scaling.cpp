#include "bench_scaling.hpp"
#include "bench_throughput.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/common/time/time.hpp"
#include "core/common/time/sleep.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <atomic>
#include <chrono>
#include <vector>

static constexpr uint64_t kSpinWaitTimeoutNs = 30000000000ULL; // 30초

ScalingParams ScalingParams::parse(const IBenchmark::Args& args){
    ScalingParams p;
    for(auto& [k, v] : args){
        try{
            if(k == "actors") p.actors = std::stoi(v);
            else if(k == "workers") p.workers = std::stoi(v);
            else if(k == "iterations") p.iterations = std::stoi(v);
            else if(k == "maxbatch") p.maxbatch = std::stoi(v);
            else if(k == "warmup") p.warmup = std::stoi(v);
            else if(k == "scale-max") p.scaleMax = std::stoi(v);
        }catch(const std::exception&){
        }
    }
    if(p.actors < 1) p.actors = 1;
    if(p.workers < 1) p.workers = 1;
    if(p.iterations < 1) p.iterations = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.warmup < 0) p.warmup = 0;
    if(p.scaleMax < 1) p.scaleMax = 1;
    return p;
}

IBenchmark::Result ScalingBenchmark::run(const Args& args){
    bool wasMetricsEnabled = Metrics::isEnabled();
    ScalingParams p = ScalingParams::parse(args);

    Metrics::setEnabled(false);

    auto measureThroughput = [&](int workers, int actors, int iterations) -> double{
        std::atomic<uint64_t> cnt{0};
        ActorSystem sys(workers, p.maxbatch);
        std::vector<BenchActor*> acts;
        for(int i = 0; i < actors; i++){
            std::string nm = "bench_" + std::to_string(i);
            size_t mbSize = static_cast<size_t>(iterations / actors) + 256;
            acts.push_back(sys.createActor<BenchActor>(nm, mbSize, cnt));
        }
        sys.start();
        auto st = Time::now();
        for(int i = 0; i < iterations; i++){
            acts[i % actors]->receiveMsg(Tick{});
        }
        auto waitStart = Time::now();
        while(cnt.load(std::memory_order_relaxed) < static_cast<uint64_t>(iterations)){
            if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) break;
            Sleep::sleepMs(100);
        }
        auto et = Time::now();
        sys.stop();
        uint64_t ns = Time::toNs(et - st);
        return (ns > 0) ? (static_cast<double>(iterations) * 1000000000.0 / static_cast<double>(ns)) : 0.0;
    };

    // Warmup
    if(p.warmup > 0){
        measureThroughput(p.workers, p.actors, p.warmup);
    }

    // Worker scaling (actors 고정)
    std::vector<std::pair<int, double>> workerResults;
    for(int w = 1; w <= p.scaleMax; w *= 2){
        double tp = measureThroughput(w, p.actors, p.iterations);
        workerResults.push_back({w, tp});
    }

    // Actor scaling (workers 고정)
    std::vector<std::pair<int, double>> actorResults;
    for(int a = 1; a <= p.scaleMax; a *= 2){
        double tp = measureThroughput(p.workers, a, p.iterations);
        actorResults.push_back({a, tp});
    }

    // 첫 번째(기준) 측정값의 처리량을 기반으로 totalDurationNs 추정
    double baseTp = workerResults.empty() ? 0.0 : workerResults.front().second;
    uint64_t estimatedDurationNs = (baseTp > 0)
        ? static_cast<uint64_t>(static_cast<double>(p.iterations) * 1000000000.0 / baseTp)
        : 0;

    IBenchmark::Result res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, p.actors, p.maxbatch, 0, p.warmup};
    res.throughput.iterations = p.iterations;
    res.throughput.totalDurationNs = estimatedDurationNs;
    res.throughput.msgsPerSec = baseTp;
    res.scalingCurve.workerScaling = std::move(workerResults);
    res.scalingCurve.actorScaling = std::move(actorResults);

    Metrics::setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("scaling", [](){
        return std::make_unique<ScalingBenchmark>();
    });
    return true;
}();
