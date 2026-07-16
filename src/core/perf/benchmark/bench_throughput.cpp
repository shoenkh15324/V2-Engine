#include "bench_throughput.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/common/time/time.hpp"
#include "core/common/time/sleep.hpp"
#include <vector>

IBenchmark::Result ThroughputBenchmark::run(const Args& args){
    bool wasMetricsEnabled = Metrics::isEnabled();
    std::atomic<uint64_t> counter{0};
    int workers = 4;
    int iterations = 10000;
    int maxbatch = 32;
    int actors = 1;
    
    for(auto& [k, v] : args){
        if(k == "workers") workers = std::stoi(v);
        if(k == "iterations") iterations = std::stoi(v);
        if(k == "maxbatch") maxbatch = std::stoi(v);
        if(k == "actors") actors = std::stoi(v);
    }

    if(workers < 1) workers = 1;
    if(iterations < 1) iterations = 1;
    if(maxbatch < 1) maxbatch = 1;
    if(actors < 1) actors = 1;
    if(actors > iterations) actors = iterations;

    int perActor = iterations / actors;

    Metrics::setEnabled(false);
    ActorSystem actorSystem(workers, maxbatch);
    std::vector<BenchActor*> benchActors;
    for(int i = 0; i < actors; i++){
        std::string name = "bench_" + std::to_string(i);
        size_t mailboxSize = static_cast<size_t>(perActor) + 256;
        auto* actor = actorSystem.createActor<BenchActor>(name, mailboxSize, counter);
        benchActors.push_back(actor);
    }

    actorSystem.start();
    auto startTime = Time::now();
    for(int i = 0; i < iterations; i++){
        benchActors[i % actors]->receiveMsg(Tick{});
    }
    while(counter.load(std::memory_order_relaxed) < static_cast<uint64_t>(iterations)){
        Sleep::sleepMs(100);
    }
    auto endTime = Time::now();
    actorSystem.stop();

    IBenchmark::Result res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {workers, actors, maxbatch, static_cast<size_t>(perActor) + 256};
    res.iterations = iterations;
    res.totalDurationNs = Time::toNs(endTime - startTime);
    res.throughputPerSec = (res.totalDurationNs > 0) ? (static_cast<double>(iterations) * 1000000000.0 / static_cast<double>(res.totalDurationNs)) : 0.0;
    res.avgLatencyNs = (iterations > 0) ? (static_cast<double>(res.totalDurationNs) / static_cast<double>(iterations)) : 0.0;
    for(auto* actor : benchActors){
        res.actorSnaps.push_back({actor->actorName(), actor->mailboxCapacity(), actor->processed()});
    }

    Metrics::setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("throughput", [](){
        return std::make_unique<ThroughputBenchmark>();
    });
    return true;
}();
