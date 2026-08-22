#include "bench_scaling.hpp"
#include "benchmark.hpp"
#include "bench_throughput.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "bench/event_loop_factory.hpp"
#include "core/common/time/time.hpp"
#include "service/tick/tick_messages.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

static constexpr uint64_t kSpinWaitTimeoutNs = 30000000000ULL; // 30초

ScalingParams ScalingParams::parse(const IBenchmark::Args& args){
    ScalingParams p;
    for(auto& [k, v] : args){
        try{
            if(k == "actors") p.actors = std::stoi(v);
            else if(k == "workers") p.workers = std::stoi(v);
            else if(k == "iterations") p.iterations = std::stoi(v);
            else if(k == "maxbatch") p.maxbatch = std::stoi(v);
            else if(k == "warmup") p.warmup = std::stoi(v);
            else if(k == "scale-max") p.scaleMax = std::stoi(v);
        }catch(const std::exception&){
        }
    }
    if(p.actors < 1) p.actors = 1;
    if(p.workers < 1) p.workers = 1;
    if(p.iterations < 1) p.iterations = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.warmup < 0) p.warmup = 0;
    if(p.scaleMax < 1) p.scaleMax = 1;
    return p;
}

namespace{

struct RunOutcome{
    bool completed{false};
    uint64_t elapsedNs{0};
    uint64_t handled{0};     // 벤치 측 처리 수 (실패 메시지용)
    uint64_t accepted{0};    // Metrics enqueued 합 (collectMetrics=true만 유효)
    uint64_t dropped{0};
    uint64_t processed{0};
    uint64_t remaining{0};
};

// config 하나당 새 ActorSystem으로 독립 측정.
// collectMetrics=false: 성능 실측(Phase A). true: 카운터 수집(Phase B).
RunOutcome runOnce(int workers, int actors, int iterations, int maxbatch, bool collectMetrics){
    RunOutcome out;
    V2_METRICS()->setEnabled(collectMetrics);

    std::atomic<uint64_t> cnt{0};
    auto sys = createDefaultActorSystem({workers, maxbatch}, bench::makeDefaultEventLoop());
    std::vector<BenchActor*> acts;
    for(int i = 0; i < actors; i++){
        std::string nm = "bench_" + std::to_string(i);
        size_t mbSize = static_cast<size_t>(iterations / actors) + 256;
        acts.push_back(sys->createActor<BenchActor>(nm, mbSize, cnt));
    }
    if(collectMetrics) V2_METRICS()->reset(); // 이전 실행 누적 제거

    sys->start();
    auto st = Time::now();
    for(int i = 0; i < iterations; i++){
        acts[i % actors]->receiveMsg(Message::make(Tick{}));
    }
    auto waitStart = Time::now();
    while(cnt.load(std::memory_order_relaxed) < static_cast<uint64_t>(iterations)){
        if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) break;
    }
    auto et = Time::now();

    out.completed = (cnt.load(std::memory_order_relaxed) >= static_cast<uint64_t>(iterations));
    out.elapsedNs = Time::toNs(et - st);
    out.handled = cnt.load(std::memory_order_relaxed);

    sys->stop(); // 워커 조인 → 이후 스냅샷/카운트는 최종값 보장

    if(collectMetrics){
        auto snap = V2_METRICS()->snapshot();
        for(auto& a : snap.actors){
            out.accepted += a.data.enqueued;
            out.dropped += a.data.dropped;
            out.processed += a.data.processed;
        }
        uint64_t remain = 0;
        for(auto* act : acts) remain += act->mailboxCount();
        out.remaining = remain;
    }

    return out;
}

} // namespace

BenchmarkResult ScalingBenchmark::run(const Args& args){
    bool wasMetricsEnabled = V2_METRICS()->isEnabled();
    ScalingParams p = ScalingParams::parse(args);

    auto fail = [&](const std::string& msg){
        BenchmarkResult res;
        res.benchmarkName = name();
        res.description = description();
        res.config = {p.workers, p.actors, p.maxbatch, 0, p.warmup};
        res.success = false;
        res.errorMsg = msg;
        V2_METRICS()->setEnabled(wasMetricsEnabled);
        return res;
    };

    if(p.warmup > 0){
        runOnce(p.workers, p.actors, std::min(p.iterations, 10000), p.maxbatch, false);
    }

    // config 하나 = Phase A(성능, metrics off) + Phase B(검증 재실행, metrics on)
    auto measure = [&](int workers, int actors, ScalePoint& pt) -> std::string{
        pt.produced = static_cast<uint64_t>(p.iterations);

        RunOutcome perf = runOnce(workers, actors, p.iterations, p.maxbatch, false);
        if(!perf.completed){
            return "incomplete(perf): handled=" + std::to_string(perf.handled)
                 + "/" + std::to_string(pt.produced);
        }
        pt.elapsedNs = perf.elapsedNs;
        pt.msgsPerSec = (perf.elapsedNs > 0)
            ? (static_cast<double>(pt.produced) * 1e9 / static_cast<double>(perf.elapsedNs))
            : 0.0;

        RunOutcome verif = runOnce(workers, actors, p.iterations, p.maxbatch, true);
        pt.accepted = verif.accepted;
        pt.dropped = verif.dropped;
        pt.processed = verif.processed;
        pt.remaining = verif.remaining;

        if(!verif.completed){
            return "incomplete(verify): handled=" + std::to_string(verif.handled)
                 + "/" + std::to_string(pt.produced);
        }
        if(pt.produced != pt.accepted + pt.dropped){
            return "P!=A+D: P=" + std::to_string(pt.produced)
                 + " A=" + std::to_string(pt.accepted)
                 + " D=" + std::to_string(pt.dropped);
        }
        if(pt.accepted != pt.processed + pt.remaining || pt.remaining != 0){
            return "A!=Pr+R: A=" + std::to_string(pt.accepted)
                 + " Pr=" + std::to_string(pt.processed)
                 + " R=" + std::to_string(pt.remaining);
        }
        return "";
    };

    std::vector<ScalePoint> workerPoints;
    for(int w = 1; w <= p.scaleMax; w *= 2){
        ScalePoint pt;
        pt.param = w;
        std::string err = measure(w, p.actors, pt);
        if(!err.empty()) return fail("scaling failed at w=" + std::to_string(w) + ": " + err);
        workerPoints.push_back(std::move(pt));
    }

    std::vector<ScalePoint> actorPoints;
    for(int a = 1; a <= p.scaleMax; a *= 2){
        ScalePoint pt;
        pt.param = a;
        std::string err = measure(p.workers, a, pt);
        if(!err.empty()) return fail("scaling failed at a=" + std::to_string(a) + ": " + err);
        actorPoints.push_back(std::move(pt));
    }

    // 요약은 Phase A 실측 합산만 사용 — Total Time/Throughput/Iterations 상호 일치 (#1)
    uint64_t totalMsgs = 0, totalNs = 0;
    for(auto& q : workerPoints){ totalMsgs += q.produced; totalNs += q.elapsedNs; }
    for(auto& q : actorPoints){ totalMsgs += q.produced; totalNs += q.elapsedNs; }

    BenchmarkResult res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, p.actors, p.maxbatch, 0, p.warmup};
    res.throughput.iterations = totalMsgs;
    res.throughput.totalDurationNs = totalNs;
    res.throughput.msgsPerSec = (totalNs > 0)
        ? (static_cast<double>(totalMsgs) * 1e9 / static_cast<double>(totalNs))
        : 0.0;
    res.scaling.workerScaling = std::move(workerPoints);
    res.scaling.actorScaling = std::move(actorPoints);

    V2_METRICS()->setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("scaling", [](){
        return std::make_unique<ScalingBenchmark>();
    });
    return true;
}();
