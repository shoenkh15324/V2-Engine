#include "bench_latency.hpp"
#include "benchmark.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/common/time/time.hpp"
#include "core/common/time/sleep.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <algorithm>
#include <chrono>

static constexpr uint64_t kSpinWaitTimeoutNs = 30000000000ULL; // 30초

LatencyParams LatencyParams::parse(const IBenchmark::Args& args){
    LatencyParams p;
    for(auto& [k, v] : args){
        try{
            if(k == "workers") p.workers = std::stoi(v);
            else if(k == "iterations") p.iterations = std::stoi(v);
            else if(k == "maxbatch") p.maxbatch = std::stoi(v);
            else if(k == "warmup") p.warmup = std::stoi(v);
            else if(k == "mailbox") p.mailbox = std::stoul(v);
        }catch(const std::exception&){
        }
    }
    if(p.workers < 1) p.workers = 1;
    if(p.iterations < 1) p.iterations = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.warmup < 0) p.warmup = 0;
    return p;
}

BenchmarkResult LatencyBenchmark::run(const Args& args){
    bool wasMetricsEnabled = Metrics::isEnabled();
    LatencyParams p = LatencyParams::parse(args);

    Metrics::setEnabled(false);

    auto runOnce = [&](int iters){
        std::vector<uint64_t> lats(iters);
        std::vector<int64_t> sendTs(iters);
        std::atomic<uint64_t> ack{0};

        ActorSystem sys(p.workers, p.maxbatch);
        size_t mbSize = (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(iters) + 256;
        auto* act = sys.createActor<LatencyBenchActor>("latency_actor", mbSize, lats, sendTs, ack);
        sys.start();
        auto st = Time::now();
        for(int i = 0; i < iters; i++){
            sendTs[i] = Time::nowNs();
            act->receiveMsg(Tick{});
            auto waitStart = Time::now();
            while(ack.load(std::memory_order_acquire) < static_cast<uint64_t>(i + 1)){
                if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) break;
            }
        }
        auto et = Time::now();
        sys.stop();
    };

    if(p.warmup > 0) runOnce(p.warmup);

    std::vector<uint64_t> latencies(p.iterations);
    std::vector<int64_t> sendTimestamps(p.iterations);
    std::atomic<uint64_t> ackCounter{0};

    ActorSystem actorSystem(p.workers, p.maxbatch);
    size_t mailboxSize = (p.mailbox > 0) ? p.mailbox : static_cast<size_t>(p.iterations) + 256;
    auto* actor = actorSystem.createActor<LatencyBenchActor>(
        "latency_actor", mailboxSize,
        latencies, sendTimestamps, ackCounter
    );

    actorSystem.start();
    auto startTime = Time::now();

    for(int i = 0; i < p.iterations; i++){
        sendTimestamps[i] = Time::nowNs();
        actor->receiveMsg(Tick{});
        auto waitStart = Time::now();
        while(ackCounter.load(std::memory_order_acquire) < static_cast<uint64_t>(i + 1)){
            if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) break;
        }
    }

    auto endTime = Time::now();
    actorSystem.stop();

    std::sort(latencies.begin(), latencies.end());
    size_t n = latencies.size();

    double sum = 0.0;
    for(auto lat : latencies) sum += static_cast<double>(lat);

    BenchmarkResult res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, 1, p.maxbatch, mailboxSize, p.warmup};
    res.throughput.iterations = p.iterations;
    res.throughput.totalDurationNs = static_cast<uint64_t>(Time::toNs(endTime - startTime));
    res.throughput.msgsPerSec = (res.throughput.totalDurationNs > 0)
        ? (static_cast<double>(p.iterations) * 1000000000.0 / static_cast<double>(res.throughput.totalDurationNs))
        : 0.0;
    res.latency.avgNs = sum / static_cast<double>(n);
    res.latency.minNs = static_cast<double>(latencies.front());
    res.latency.maxNs = static_cast<double>(latencies.back());
    res.latency.percentiles.p50 = static_cast<double>(latencies[n * 50 / 100]);
    res.latency.percentiles.p95 = static_cast<double>(latencies[n * 95 / 100]);
    res.latency.percentiles.p99 = static_cast<double>(latencies[n * 99 / 100]);
    res.latency.percentiles.p999 = static_cast<double>(latencies[std::min(n * 999 / 1000, n - 1)]);
    res.actorSnaps.push_back({"latency_actor", actor->mailboxCapacity(), actor->processed()});

    Metrics::setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("latency", [](){
        return std::make_unique<LatencyBenchmark>();
    });
    return true;
}();
