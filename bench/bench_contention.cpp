#include "bench_contention.hpp"
#include "benchmark.hpp"
#include "bench_throughput.hpp"
#include "core/actor_system/actor_system.hpp"
#include "bench/event_loop_factory.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/time/time.hpp"
#include "service/tick/tick_messages.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

static constexpr uint64_t kSpinWaitTimeoutNs = 10000000000ULL; // 10초

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
    bool wasMetricsEnabled = V2_METRICS()->isEnabled();
    ContentionParams p = ContentionParams::parse(args);

    V2_METRICS()->setEnabled(false);

    struct Outcome{
        bool completed{false};
        uint64_t elapsedNs{0};
        uint64_t handled{0};
    };

    auto runOnce = [&](int numProducers, int64_t msgsPerProducer) -> Outcome{
        int64_t total = static_cast<int64_t>(numProducers) * msgsPerProducer;
        auto sys = createDefaultActorSystem({p.workers, p.maxbatch}, bench::makeDefaultEventLoop());
        size_t mbSize = (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(total) + 256;
        auto* actor = sys->createActor<BenchActor>("contention_actor", mbSize);
        sys->start();

        // start barrier: 스레드 생성 비용을 측정 구간 밖으로 (#6)
        std::atomic<bool> go{false};
        std::vector<std::thread> producers;
        for(int i = 0; i < numProducers; i++){
            producers.emplace_back([&]{
                while(!go.load(std::memory_order_acquire)) std::this_thread::yield();
                for(int64_t j = 0; j < msgsPerProducer; j++){
                    actor->receiveMsg(Message::make(Tick{}));
                }
            });
        }

        auto st = Time::now();
        go.store(true, std::memory_order_release);
        for(auto& t : producers) t.join();
        auto waitStart = Time::now();
        while(actor->processed() < static_cast<uint64_t>(total)){
            if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) break;
        }
        auto et = Time::now();
        sys->stop();

        Outcome out;
        out.completed = (actor->processed() >= static_cast<uint64_t>(total));
        out.elapsedNs = Time::toNs(et - st);
        out.handled = actor->processed();
        return out;
    };

    if(p.warmup > 0) runOnce(p.producers, static_cast<int64_t>(p.warmup));

    int64_t msgsPerProducer = static_cast<int64_t>(p.iterations) / p.producers;
    int64_t actualTotal = static_cast<int64_t>(p.producers) * msgsPerProducer;
    Outcome out = runOnce(p.producers, msgsPerProducer);

    BenchmarkResult res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, 1, p.maxbatch, (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(actualTotal) + 256, p.warmup};

    if(!out.completed){
        res.success = false;
        res.errorMsg = "contention incomplete: handled=" + std::to_string(out.handled)
                     + "/" + std::to_string(actualTotal);
        V2_METRICS()->setEnabled(wasMetricsEnabled);
        return res;
    }

    res.throughput.iterations = static_cast<uint64_t>(actualTotal);
    res.throughput.totalDurationNs = out.elapsedNs;
    res.throughput.msgsPerSec = (out.elapsedNs > 0) ? (static_cast<double>(actualTotal) * 1000000000.0 / static_cast<double>(out.elapsedNs)) : 0.0;

    V2_METRICS()->setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("contention", [](){
        return std::make_unique<ContentionBenchmark>();
    });
    return true;
}();
