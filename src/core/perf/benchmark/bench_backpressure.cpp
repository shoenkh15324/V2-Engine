#include "bench_backpressure.hpp"
#include "bench_throughput.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/common/time/time.hpp"
#include "core/common/time/sleep.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <atomic>
#include <chrono>
#include <vector>

static constexpr uint64_t kSpinWaitTimeoutNs = 30000000000ULL; // 30초

BackpressureParams BackpressureParams::parse(const IBenchmark::Args& args){
    BackpressureParams p;
    for(auto& [k, v] : args){
        try{
            if(k == "workers") p.workers = std::stoi(v);
            else if(k == "maxbatch") p.maxbatch = std::stoi(v);
            else if(k == "warmup") p.warmup = std::stoi(v);
            else if(k == "mailbox") p.mailbox = std::stoul(v);
            else if(k == "flood-rate") p.floodRate = std::stoi(v);
            else if(k == "flood-duration") p.floodDurationMs = std::stoi(v);
        }catch(const std::exception&){
        }
    }
    if(p.workers < 1) p.workers = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.warmup < 0) p.warmup = 0;
    if(p.mailbox < 1) p.mailbox = 64;
    if(p.floodRate < 1) p.floodRate = 1000000;
    if(p.floodDurationMs < 1) p.floodDurationMs = 100;
    return p;
}

IBenchmark::Result BackpressureBenchmark::run(const Args& args){
    bool wasMetricsEnabled = Metrics::isEnabled();
    BackpressureParams p = BackpressureParams::parse(args);

    Metrics::setEnabled(false);

    std::atomic<uint64_t> sent{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> processed{0};

    ActorSystem actorSystem(p.workers, p.maxbatch);
    auto* actor = actorSystem.createActor<BenchActor>("bp_actor", p.mailbox, processed);
    actorSystem.start();

    // Flood
    auto floodStart = Time::now();
    int64_t floodDurationNs = static_cast<int64_t>(p.floodDurationMs) * 1000000;
    double intervalPerMsgNs = 1000000000.0 / static_cast<double>(p.floodRate);
    int64_t totalToSend = std::max(static_cast<int64_t>(1),
        static_cast<int64_t>(p.floodRate) * p.floodDurationMs / 1000);

    for(int64_t i = 0; i < totalToSend; i++){
        bool ok = actor->mailboxCount() < actor->mailboxCapacity();
        if(ok){
            actor->receiveMsg(Tick{});
            sent.fetch_add(1, std::memory_order_relaxed);
        }else{
            dropped.fetch_add(1, std::memory_order_relaxed);
        }
        // Rate limiting
        int64_t targetNs = static_cast<int64_t>(static_cast<double>(i + 1) * intervalPerMsgNs);
        while(Time::toNs(Time::now() - floodStart) < targetNs){
        }
    }
    auto floodEnd = Time::now();

    // Wait for drain
    auto drainStart = Time::now();
    auto drainWaitStart = Time::now();
    while(actor->mailboxCount() > 0){
        if(Time::toNs(Time::now() - drainWaitStart) > kSpinWaitTimeoutNs) break;
        Sleep::sleepMs(1);
    }
    auto drainEnd = Time::now();

    actorSystem.stop();

    uint64_t sentVal = sent.load(std::memory_order_relaxed);
    uint64_t droppedVal = dropped.load(std::memory_order_relaxed);
    uint64_t floodDuration = Time::toNs(floodEnd - floodStart);
    uint64_t drainDuration = Time::toNs(drainEnd - drainStart);
    double dropRate = (sentVal + droppedVal > 0)
        ? (static_cast<double>(droppedVal) * 100.0 / static_cast<double>(sentVal + droppedVal))
        : 0.0;

    IBenchmark::Result res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, 1, p.maxbatch, p.mailbox, p.warmup};
    res.throughput.iterations = sentVal;
    res.throughput.totalDurationNs = floodDuration;
    res.throughput.msgsPerSec = (floodDuration > 0)
        ? (static_cast<double>(sentVal) * 1000000000.0 / static_cast<double>(floodDuration))
        : 0.0;
    res.backpressure.sent = sentVal;
    res.backpressure.dropped = droppedVal;
    res.backpressure.dropRate = dropRate;
    res.backpressure.floodDurationNs = floodDuration;
    res.backpressure.drainDurationNs = drainDuration;

    Metrics::setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("backpressure", [](){
        return std::make_unique<BackpressureBenchmark>();
    });
    return true;
}();
