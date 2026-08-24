#include "bench_throughput.hpp"
#include "benchmark.hpp"
#include "bench/src/bench_common.hpp"
#include "bench/src/bench_load.hpp"
#include "core/actor_system/actor_system.hpp"
#include "bench/src/event_loop_factory.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/time/time.hpp"
#include "service/tick/tick_messages.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace {

struct RunOutcome{
    bool completed{false};
    uint64_t elapsedNs{0};
    uint64_t handled{0};
};

std::unique_ptr<ActorSystem> makeSystem(const ThroughputParams& p){
    ActorSystemConfig sysCfg;
    sysCfg.numWorkers = p.workers;
    sysCfg.maxBatch = p.maxbatch;
    if(p.parkSpinNs >= 0) sysCfg.parkSpinNs = p.parkSpinNs;
    if(p.tokenGraceNs >= 0) sysCfg.tokenGraceNs = p.tokenGraceNs;
    return createDefaultActorSystem(sysCfg, bench::makeDefaultEventLoop());
}

std::vector<BenchActor*> createActors(ActorSystem& sys, const ThroughputParams& p, int64_t iterations){
    std::vector<BenchActor*> acts;
    size_t mbSize = (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(iterations / p.actors) + 256;
    for(int i = 0; i < p.actors; ++i){
        acts.push_back(sys.createActor<BenchActor>("bench_" + std::to_string(i), mbSize));
    }
    return acts;
}

RunOutcome runOnce(const ThroughputParams& p, int64_t iterations){
    auto sys = makeSystem(p);
    std::vector<BenchActor*> acts = createActors(*sys, p, iterations);
    sys->start();

    auto start = bench::publishMessages(acts, p.producers, iterations);

    auto waitStart = Time::now();
    bool completed = bench::waitForTotal([&]{ return bench::totalProcessed(acts); },
                                         static_cast<uint64_t>(iterations), waitStart);
    auto end = Time::now();
    sys->stop();

    return { completed, static_cast<uint64_t>(Time::toNs(end - start)), bench::totalProcessed(acts) };
}

} // namespace

ThroughputParams ThroughputParams::parse(const IBenchmark::Args& args){
    ThroughputParams p;
    for(auto& [k, v] : args){
        try{
            if(k == "workers") p.workers = std::stoi(v);
            else if(k == "actors") p.actors = std::stoi(v);
            else if(k == "iterations") p.iterations = std::stoi(v);
            else if(k == "maxbatch") p.maxbatch = std::stoi(v);
            else if(k == "warmup") p.warmup = std::stoi(v);
            else if(k == "producers") p.producers = std::stoi(v);
            else if(k == "park-spin-ns") p.parkSpinNs = std::stoi(v);
            else if(k == "token-grace-ns") p.tokenGraceNs = std::stoi(v);
            else if(k == "mailbox") p.mailbox = std::stoul(v);
        }catch(const std::exception&){
        }
    }
    if(p.workers < 1) p.workers = 1;
    if(p.iterations < 1) p.iterations = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.actors < 1) p.actors = 1;
    if(p.actors > p.iterations) p.actors = p.iterations;
    if(p.warmup < 0) p.warmup = 0;
    if(p.producers < 1) p.producers = 1;
    return p;
}

BenchmarkResult ThroughputBenchmark::run(const Args& args){
    bool wasMetricsEnabled = V2_METRICS()->isEnabled();
    ThroughputParams p = ThroughputParams::parse(args);

    bool diag = std::getenv("V2_DIAG") != nullptr;
    if(diag) V2_METRICS()->init(static_cast<size_t>(p.workers));
    V2_METRICS()->setEnabled(diag);

    if(p.warmup > 0) runOnce(p, p.warmup);
    RunOutcome out = runOnce(p, p.iterations);

    BenchmarkResult res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, p.actors, p.maxbatch,
                  (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(p.iterations / p.actors) + 256,
                  p.warmup};

    if(!out.completed){
        res.success = false;
        res.errorMsg = "throughput incomplete: handled=" + std::to_string(out.handled)
                     + "/" + std::to_string(p.iterations);
        V2_METRICS()->setEnabled(wasMetricsEnabled);
        return res;
    }

    res.throughput.iterations = p.iterations;
    res.throughput.totalDurationNs = out.elapsedNs;
    res.throughput.msgsPerSec = (out.elapsedNs > 0)
        ? (static_cast<double>(p.iterations) * 1000000000.0 / static_cast<double>(out.elapsedNs))
        : 0.0;

    V2_METRICS()->setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("throughput", [](){
        return std::make_unique<ThroughputBenchmark>();
    });
    return true;
}();
