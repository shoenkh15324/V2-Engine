#include "bench_contention.hpp"
#include "benchmark.hpp"
#include "bench_throughput.hpp"
#include "bench/src/bench_common.hpp"
#include "bench/src/bench_load.hpp"
#include "core/actor_system/actor_system.hpp"
#include "bench/src/event_loop_factory.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/time/time.hpp"
#include "service/tick/tick_messages.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace {

struct RunOutcome{
    bool completed{false};
    uint64_t elapsedNs{0};
    uint64_t handled{0};
};

// 단일 핫 액터로 멀티 프로듀서 경합을 측정한다.
RunOutcome runOnce(const ContentionParams& p, int producers, int64_t msgsPerProducer){
    const int64_t total = static_cast<int64_t>(producers) * msgsPerProducer;
    auto sys = createDefaultActorSystem({p.workers, p.maxbatch}, bench::makeDefaultEventLoop());
    size_t mbSize = (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(total) + 256;
    auto* actor = sys->createActor<BenchActor>("contention_actor", mbSize);
    sys->start();

    std::vector<BenchActor*> acts{actor};
    auto start = bench::publishMessages(acts, producers, total);

    auto waitStart = Time::now();
    bool completed = bench::waitForTotal([&]{ return actor->processed(); },
                                         static_cast<uint64_t>(total), waitStart);
    auto end = Time::now();
    sys->stop();

    return { completed, static_cast<uint64_t>(Time::toNs(end - start)), actor->processed() };
}

} // namespace

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

    if(p.warmup > 0) runOnce(p, p.producers, p.warmup);

    int64_t msgsPerProducer = static_cast<int64_t>(p.iterations) / p.producers;
    int64_t actualTotal = static_cast<int64_t>(p.producers) * msgsPerProducer;
    RunOutcome out = runOnce(p, p.producers, msgsPerProducer);

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
