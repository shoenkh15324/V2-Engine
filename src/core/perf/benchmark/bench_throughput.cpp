#include "bench_throughput.hpp"
#include "benchmark.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/common/time/time.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <chrono>
#include <vector>

static constexpr uint64_t kSpinWaitTimeoutNs = 30000000000ULL; // 30초

ThroughputParams ThroughputParams::parse(const IBenchmark::Args& args){
    ThroughputParams p;
    for(auto& [k, v] : args){
        try{
            if(k == "workers") p.workers = std::stoi(v);
            else if(k == "actors") p.actors = std::stoi(v);
            else if(k == "iterations") p.iterations = std::stoi(v);
            else if(k == "maxbatch") p.maxbatch = std::stoi(v);
            else if(k == "warmup") p.warmup = std::stoi(v);
            else if(k == "mailbox") p.mailbox = std::stoul(v);
        }catch(const std::exception&){
            // 파싱 실패 시 기본값 유지
        }
    }
    if(p.workers < 1) p.workers = 1;
    if(p.iterations < 1) p.iterations = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.actors < 1) p.actors = 1;
    if(p.actors > p.iterations) p.actors = p.iterations;
    if(p.warmup < 0) p.warmup = 0;
    return p;
}

BenchmarkResult ThroughputBenchmark::run(const Args& args){
    bool wasMetricsEnabled = Metrics::isEnabled();
    ThroughputParams p = ThroughputParams::parse(args);
    int perActor = p.iterations / p.actors;

    Metrics::setEnabled(false);

    auto runOnce = [&](int iters) -> uint64_t{
        std::atomic<uint64_t> cnt{0};
        ActorSystem sys(p.workers, p.maxbatch);
        std::vector<BenchActor*> acts;
        for(int i = 0; i < p.actors; i++){
            std::string nm = "bench_" + std::to_string(i);
            size_t mbSize = (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(iters / p.actors) + 256;
            acts.push_back(sys.createActor<BenchActor>(nm, mbSize, cnt));
        }
        sys.start();
        auto st = Time::now();
        for(int i = 0; i < iters; i++){
            acts[i % p.actors]->receiveMsg(Tick{});
        }
        auto waitStart = Time::now();
        while(cnt.load(std::memory_order_relaxed) < static_cast<uint64_t>(iters)){
            if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) break;
        }
        auto et = Time::now();
        sys.stop();
        return Time::toNs(et - st);
    };

    if(p.warmup > 0) runOnce(p.warmup);

    uint64_t totalNs = runOnce(p.iterations);

    BenchmarkResult res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, p.actors, p.maxbatch, (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(perActor) + 256, p.warmup};
    res.throughput.iterations = p.iterations;
    res.throughput.totalDurationNs = totalNs;
    res.throughput.msgsPerSec = (totalNs > 0)
        ? (static_cast<double>(p.iterations) * 1000000000.0 / static_cast<double>(totalNs))
        : 0.0;

    Metrics::setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("throughput", [](){
        return std::make_unique<ThroughputBenchmark>();
    });
    return true;
}();
