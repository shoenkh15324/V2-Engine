#include "bench_contention.hpp"
#include "benchmark.hpp"
#include "bench_throughput.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/common/time/time.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

static constexpr uint64_t kSpinWaitTimeoutNs = 30000000000ULL; // 30초

ContentionParams ContentionParams::parse(const IBenchmark::Args& args){
    ContentionParams p;
    for(auto& [k, v] : args){
        try{
            if(k == "workers") p.workers = std::stoi(v);
            else if(k == "producers") p.producers = std::stoi(v);
            else if(k == "iterations") p.iterations = std::stoi(v);
            else if(k == "maxbatch") p.maxbatch = std::stoi(v);
            else if(k == "warmup") p.warmup = std::stoi(v);
            else if(k == "mailbox") p.mailbox = std::stoul(v);
        }catch(const std::exception&){
        }
    }
    if(p.workers < 1) p.workers = 1;
    if(p.producers < 1) p.producers = 1;
    if(p.iterations < 1) p.iterations = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.warmup < 0) p.warmup = 0;
    return p;
}

BenchmarkResult ContentionBenchmark::run(const Args& args){
    bool wasMetricsEnabled = Metrics::isEnabled();
    ContentionParams p = ContentionParams::parse(args);

    Metrics::setEnabled(false);

    auto runOnce = [&](int numProducers, int64_t msgsPerProducer) -> uint64_t{
        int64_t total = static_cast<int64_t>(numProducers) * msgsPerProducer;
        std::atomic<uint64_t> cnt{0};
        ActorSystem sys(p.workers, p.maxbatch);
        size_t mbSize = (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(total) + 256;
        auto* actor = sys.createActor<BenchActor>("contention_actor", mbSize, cnt);
        sys.start();
        auto st = Time::now();
        std::vector<std::thread> producers;
        for(int i = 0; i < numProducers; i++){
            producers.emplace_back([&]{
                for(int64_t j = 0; j < msgsPerProducer; j++){
                    actor->receiveMsg(Tick{});
                }
            });
        }
        for(auto& t : producers) t.join();
        auto waitStart = Time::now();
        while(cnt.load(std::memory_order_relaxed) < static_cast<uint64_t>(total)){
            if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) break;
        }
        auto et = Time::now();
        sys.stop();
        return Time::toNs(et - st);
    };

    if(p.warmup > 0) runOnce(p.producers, static_cast<int64_t>(p.warmup));

    int64_t msgsPerProducer = static_cast<int64_t>(p.iterations) / p.producers;
    int64_t actualTotal = static_cast<int64_t>(p.producers) * msgsPerProducer;
    uint64_t totalNs = runOnce(p.producers, msgsPerProducer);

    BenchmarkResult res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, 1, p.maxbatch, (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(actualTotal) + 256, p.warmup};
    res.throughput.iterations = static_cast<uint64_t>(actualTotal);
    res.throughput.totalDurationNs = totalNs;
    res.throughput.msgsPerSec = (totalNs > 0) ? (static_cast<double>(actualTotal) * 1000000000.0 / static_cast<double>(totalNs)) : 0.0;

    Metrics::setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("contention", [](){
        return std::make_unique<ContentionBenchmark>();
    });
    return true;
}();
