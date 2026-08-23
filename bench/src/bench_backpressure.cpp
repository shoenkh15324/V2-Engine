#include "bench_backpressure.hpp"
#include "benchmark.hpp"
#include "bench_throughput.hpp"
#include "core/actor_system/actor_system.hpp"
#include "bench/src/event_loop_factory.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/time/time.hpp"
#include "service/tick/tick_messages.hpp"
#include <atomic>
#include <chrono>
#include <vector>
#include <thread>

static constexpr uint64_t kDrainTimeoutNs = 10000000000ULL; // 10초

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
    bool wasMetricsEnabled = V2_METRICS()->isEnabled();
    BackpressureParams p = BackpressureParams::parse(args);

    V2_METRICS()->setEnabled(false);

    std::atomic<uint64_t> sent{0};
    std::atomic<uint64_t> dropped{0};

    auto actorSystem = createDefaultActorSystem({p.workers, p.maxbatch}, bench::makeDefaultEventLoop());
    auto* actor = actorSystem->createActor<BenchActor>("bp_actor", p.mailbox);
    size_t cap = actor->mailboxCapacity();
    int64_t totalToSend = std::max(static_cast<int64_t>(1), static_cast<int64_t>(p.floodRate) * p.floodDurationMs);

    auto drainUntil = [&](uint64_t target) -> bool{
        auto start = Time::now();
        while(actor->processed() < target){
            if(Time::toNs(Time::now() - start) > kDrainTimeoutNs) return false;
            std::this_thread::yield();
        }
        return true;
    };

    // 공용 후처리: 시스템 종료 + 결과 조립 + 타임아웃 마킹
    auto finishResult = [&](uint64_t floodDuration, uint64_t drainDuration,
                            uint64_t backlogAtDrain, bool drained){
        actorSystem->stop();

        uint64_t sentVal = sent.load(std::memory_order_relaxed);
        uint64_t droppedVal = dropped.load(std::memory_order_relaxed);
        double dropRate = (sentVal + droppedVal > 0)
            ? (static_cast<double>(droppedVal) * 100.0 / static_cast<double>(sentVal + droppedVal))
            : 0.0;

        BenchmarkResult res;
        res.benchmarkName = name();
        res.description = description();
        res.config = {p.workers, 1, p.maxbatch, p.mailbox, p.warmup};
        res.backpressure.sent = sentVal;
        res.backpressure.dropped = droppedVal;
        res.backpressure.dropRate = dropRate;
        res.backpressure.floodDurationNs = floodDuration;
        res.backpressure.drainDurationNs = drainDuration;
        res.backpressure.backlogAtDrainStart = backlogAtDrain;

        if(!drained){
            res.success = false;
            res.errorMsg = "drain timeout (" + std::to_string(kDrainTimeoutNs / 1000000000ULL) + "s):"
                + " processed=" + std::to_string(actor->processed())
                + "/" + std::to_string(sentVal)
                + " dropped=" + std::to_string(droppedVal);
        }

        V2_METRICS()->setEnabled(wasMetricsEnabled);
        return res;
    };

    if(p.mode == 0){
        // Mode 0: Empty start - workers 먼저 시작, producer가 consumer보다 빠르게 생성
        actorSystem->start();

        auto floodStart = Time::now();
        for(int64_t i = 0; i < totalToSend; i++){
            bool ok = (actor->mailboxCount() < cap);
            if(ok){
                actor->receiveMsg(Message::make(Tick{}));
                sent.fetch_add(1, std::memory_order_relaxed);
            }else{
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        auto floodEnd = Time::now();

        uint64_t backlogAtDrain = sent.load(std::memory_order_relaxed)
                                - actor->processed();

        auto drainStart = Time::now();
        bool drained = drainUntil(sent.load(std::memory_order_relaxed));
        auto drainEnd = Time::now();

        return finishResult(
            Time::toNs(floodEnd - floodStart),
            Time::toNs(drainEnd - drainStart),
            backlogAtDrain,
            drained
        );
    }
    else if(p.mode == 1){
        // Mode 1: Pre-fill - mailbox 미리 다 채우고 시작
        auto floodStart = Time::now();
        for(int64_t i = 0; i < totalToSend; i++){
            if(actor->mailboxCount() < cap){
                actor->receiveMsg(Message::make(Tick{}));
                sent.fetch_add(1, std::memory_order_relaxed);
            }else{
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        auto floodEnd = Time::now();

        uint64_t sentBefore = sent.load(std::memory_order_relaxed);
        uint64_t backlogAtDrain = sentBefore - actor->processed();

        actorSystem->start();

        auto drainStart = Time::now();
        bool drained = drainUntil(sentBefore);
        auto drainEnd = Time::now();

        return finishResult(
            Time::toNs(floodEnd - floodStart),
            Time::toNs(drainEnd - drainStart),
            backlogAtDrain,
            drained
        );
    }
    else{
        // Mode 2: Nearly full - 80% 채우고 start + flood 동시
        size_t fillTarget = cap * 80 / 100;
        auto floodStart = Time::now();
        for(size_t i = 0; i < fillTarget; i++){
            actor->receiveMsg(Message::make(Tick{}));
            sent.fetch_add(1, std::memory_order_relaxed);
        }

        actorSystem->start();

        for(int64_t i = fillTarget; i < totalToSend; i++){
            bool ok = (actor->mailboxCount() < cap);
            if(ok){
                actor->receiveMsg(Message::make(Tick{}));
                sent.fetch_add(1, std::memory_order_relaxed);
            }else{
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        auto floodEnd = Time::now();

        uint64_t backlogAtDrain = sent.load(std::memory_order_relaxed)
                                - actor->processed();

        auto drainStart = Time::now();
        bool drained = drainUntil(sent.load(std::memory_order_relaxed));
        auto drainEnd = Time::now();

        return finishResult(
            Time::toNs(floodEnd - floodStart),
            Time::toNs(drainEnd - drainStart),
            backlogAtDrain,
            drained
        );
    }
}

static bool registered = []{
    Benchmark::registerBenchmark("backpressure", [](){
        return std::make_unique<BackpressureBenchmark>();
    });
    return true;
}();
