#include "bench_throughput.hpp"
#include "benchmark.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "infra/platform/linux/event_loop_epoll.hpp"
#include "core/common/time/time.hpp"
#include "service/tick/tick_messages.hpp"
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
    bool wasMetricsEnabled = V2_METRICS()->isEnabled();
    ThroughputParams p = ThroughputParams::parse(args);
    int perActor = p.iterations / p.actors;

    V2_METRICS()->setEnabled(false);

    auto runOnce = [&](int iters) -> uint64_t{
        std::atomic<uint64_t> cnt{0};
        auto loop = std::make_unique<EventLoopEpoll>(64, 1000);
        auto sys = createDefaultActorSystem({p.workers, p.maxbatch}, std::move(loop));
        std::vector<BenchActor*> acts;
        for(int i = 0; i < p.actors; i++){
            std::string nm = "bench_" + std::to_string(i);
            size_t mbSize = (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(iters / p.actors) + 256;
            acts.push_back(sys->createActor<BenchActor>(nm, mbSize, cnt));
        }
        sys->start();
        auto st = Time::now();
        for(int i = 0; i < iters; i++){
            acts[i % p.actors]->receiveMsg(Message::make(Tick{}));
        }
        auto waitStart = Time::now();
        while(cnt.load(std::memory_order_relaxed) < static_cast<uint64_t>(iters)){
            if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) break;
        }
        auto et = Time::now();
        sys->stop();
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

    V2_METRICS()->setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("throughput", [](){
        return std::make_unique<ThroughputBenchmark>();
    });
    return true;
}();
