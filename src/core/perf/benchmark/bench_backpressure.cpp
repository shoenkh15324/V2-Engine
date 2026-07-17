#include "bench_backpressure.hpp"
#include "benchmark.hpp"
#include "bench_throughput.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/common/time/time.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <atomic>
#include <chrono>
#include <vector>
#include <thread>

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
            else if(k == "flood-threads") p.floodThreads = std::stoi(v);
            else if(k == "mode") p.mode = std::stoi(v);
        }catch(const std::exception&){
        }
    }
    if(p.workers < 1) p.workers = 1;
    if(p.warmup < 0) p.warmup = 0;
    if(p.mailbox < 1) p.mailbox = 64;
    if(p.floodRate < 1) p.floodRate = 1000;
    if(p.floodDurationMs < 1) p.floodDurationMs = 100;
    if(p.floodThreads < 1) p.floodThreads = 1;
    if(p.mode < 0 || p.mode > 2) p.mode = 0;
    return p;
}

BenchmarkResult BackpressureBenchmark::run(const Args& args){
    bool wasMetricsEnabled = Metrics::isEnabled();
    BackpressureParams p = BackpressureParams::parse(args);

    Metrics::setEnabled(false);

    std::atomic<uint64_t> sent{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> processed{0};

    ActorSystem actorSystem(p.workers, p.maxbatch);
    auto* actor = actorSystem.createActor<BenchActor>("bp_actor", p.mailbox, processed);
    size_t cap = actor->mailboxCapacity();
    int64_t totalToSend = std::max(static_cast<int64_t>(1), static_cast<int64_t>(p.floodRate) * p.floodDurationMs);

    if(p.mode == 0){
        // Mode 0: Empty start - workers 먼저 시작, producer가 consumer보다 빠르게 생성
        actorSystem.start();

        auto floodStart = Time::now();
        for(int64_t i = 0; i < totalToSend; i++){
            bool ok = (actor->mailboxCount() < cap);
            if(ok){
                actor->receiveMsg(Tick{});
                sent.fetch_add(1, std::memory_order_relaxed);
            }else{
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        auto floodEnd = Time::now();

        auto drainStart = Time::now();
        while(processed.load(std::memory_order_relaxed) < sent.load(std::memory_order_relaxed)){
        }
        auto drainEnd = Time::now();

        uint64_t floodDuration = Time::toNs(floodEnd - floodStart);
        uint64_t drainDuration = Time::toNs(drainEnd - drainStart);

        actorSystem.stop();

        uint64_t sentVal = sent.load(std::memory_order_relaxed);
        uint64_t droppedVal = dropped.load(std::memory_order_relaxed);
        double dropRate = (sentVal + droppedVal > 0) ? (static_cast<double>(droppedVal) * 100.0 / static_cast<double>(sentVal + droppedVal)) : 0.0;

        BenchmarkResult res;
        res.benchmarkName = name();
        res.description = description();
        res.config = {p.workers, 1, p.maxbatch, p.mailbox, p.warmup};
        res.backpressure.sent = sentVal;
        res.backpressure.dropped = droppedVal;
        res.backpressure.dropRate = dropRate;
        res.backpressure.floodDurationNs = floodDuration;
        res.backpressure.drainDurationNs = drainDuration;
        Metrics::setEnabled(wasMetricsEnabled);
        return res;
    }
    else if(p.mode == 1){
        // Mode 1: Pre-fill - mailbox 미리 다 채우고 시작
        auto floodStart = Time::now();
        for(int64_t i = 0; i < totalToSend; i++){
            if(actor->mailboxCount() < cap){
                actor->receiveMsg(Tick{});
                sent.fetch_add(1, std::memory_order_relaxed);
            }else{
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        auto floodEnd = Time::now();

        uint64_t sentBefore = sent.load(std::memory_order_relaxed);
        actorSystem.start();
        auto drainStart = Time::now();
        while(processed.load(std::memory_order_relaxed) < sentBefore){
        }
        auto drainEnd = Time::now();

        actorSystem.stop();

        uint64_t sentVal = sent.load(std::memory_order_relaxed);
        uint64_t droppedVal = dropped.load(std::memory_order_relaxed);
        uint64_t floodDuration = Time::toNs(floodEnd - floodStart);
        uint64_t drainDuration = Time::toNs(drainEnd - drainStart);
        double dropRate = (sentVal + droppedVal > 0) ? (static_cast<double>(droppedVal) * 100.0 / static_cast<double>(sentVal + droppedVal)) : 0.0;

        BenchmarkResult res;
        res.benchmarkName = name();
        res.description = description();
        res.config = {p.workers, 1, p.maxbatch, p.mailbox, p.warmup};
        res.backpressure.sent = sentVal;
        res.backpressure.dropped = droppedVal;
        res.backpressure.dropRate = dropRate;
        res.backpressure.floodDurationNs = floodDuration;
        res.backpressure.drainDurationNs = drainDuration;
        Metrics::setEnabled(wasMetricsEnabled);
        return res;
    }
    else{
        // Mode 2: Nearly full - 80% 채우고 start + flood 동시
        size_t fillTarget = cap * 80 / 100;
        auto floodStart = Time::now();
        for(size_t i = 0; i < fillTarget; i++){
            actor->receiveMsg(Tick{});
            sent.fetch_add(1, std::memory_order_relaxed);
        }

        actorSystem.start();

        for(int64_t i = fillTarget; i < totalToSend; i++){
            bool ok = (actor->mailboxCount() < cap);
            if(ok){
                actor->receiveMsg(Tick{});
                sent.fetch_add(1, std::memory_order_relaxed);
            }else{
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        auto floodEnd = Time::now();

        auto drainStart = Time::now();
        while(processed.load(std::memory_order_relaxed) < sent.load(std::memory_order_relaxed)){
        }
        auto drainEnd = Time::now();

        actorSystem.stop();

        uint64_t sentVal = sent.load(std::memory_order_relaxed);
        uint64_t droppedVal = dropped.load(std::memory_order_relaxed);
        uint64_t floodDuration = Time::toNs(floodEnd - floodStart);
        uint64_t drainDuration = Time::toNs(drainEnd - drainStart);
        double dropRate = (sentVal + droppedVal > 0) ? (static_cast<double>(droppedVal) * 100.0 / static_cast<double>(sentVal + droppedVal)) : 0.0;

        BenchmarkResult res;
        res.benchmarkName = name();
        res.description = description();
        res.config = {p.workers, 1, p.maxbatch, p.mailbox, p.warmup};
        res.backpressure.sent = sentVal;
        res.backpressure.dropped = droppedVal;
        res.backpressure.dropRate = dropRate;
        res.backpressure.floodDurationNs = floodDuration;
        res.backpressure.drainDurationNs = drainDuration;
        Metrics::setEnabled(wasMetricsEnabled);
        return res;
    }
}

static bool registered = []{
    Benchmark::registerBenchmark("backpressure", [](){
        return std::make_unique<BackpressureBenchmark>();
    });
    return true;
}();
