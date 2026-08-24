#include "bench_scaling.hpp"
#include "benchmark.hpp"
#include "bench_throughput.hpp"
#include "bench/src/bench_common.hpp"
#include "bench/src/bench_load.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "bench/src/event_loop_factory.hpp"
#include "core/common/time/time.hpp"
#include "service/tick/tick_messages.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

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
            else if(k == "busy-steal-us") p.busyStealUs = std::stoi(v);
            else if(k == "idle-steal-us") p.idleStealUs = std::stoi(v);
            else if(k == "producers") p.producers = std::stoi(v);
        }catch(const std::exception&){
        }
    }
    if(p.actors < 1) p.actors = 1;
    if(p.workers < 1) p.workers = 1;
    if(p.iterations < 1) p.iterations = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.warmup < 0) p.warmup = 0;
    if(p.scaleMax < 1) p.scaleMax = 1;
    if(p.producers < 1) p.producers = 1;
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

RunOutcome runOnce(int workers, int actors, int iterations, int maxbatch, bool collectMetrics, const ScalingParams& p){
    RunOutcome out;
    V2_METRICS()->setEnabled(collectMetrics);

    ActorSystemConfig sysCfg;
    sysCfg.numWorkers = workers;
    sysCfg.maxBatch = maxbatch;
    if(p.busyStealUs >= 0) sysCfg.busyStealIntervalUs = p.busyStealUs;
    if(p.idleStealUs >= 0) sysCfg.idleStealIntervalUs = p.idleStealUs;

    auto sys = createDefaultActorSystem(sysCfg, bench::makeDefaultEventLoop());
    std::vector<BenchActor*> acts;
    for(int i = 0; i < actors; i++){
        size_t mbSize = static_cast<size_t>(iterations / actors) + 256;
        acts.push_back(sys->createActor<BenchActor>("bench_" + std::to_string(i), mbSize));
    }
    if(collectMetrics) V2_METRICS()->reset();

    sys->start();
    auto start = bench::publishMessages(acts, p.producers, iterations);

    auto waitStart = Time::now();
    out.completed = bench::waitForTotal([&]{ return bench::totalProcessed(acts); },
                                        static_cast<uint64_t>(iterations), waitStart);
    auto end = Time::now();

    out.elapsedNs = Time::toNs(end - start);
    out.handled = bench::totalProcessed(acts);

    sys->stop();

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

// config 하나 = Phase A(성능, metrics off) + Phase B(검증 재실행, metrics on).
// 보존 검증(P=A+D, A=Pr+R, R=0) 실패 시 오류 문자열 반환.
std::string measurePoint(const ScalingParams& p, int workers, int actors, ScalePoint& pt){
    pt.produced = static_cast<uint64_t>(p.iterations);

    RunOutcome perf = runOnce(workers, actors, p.iterations, p.maxbatch, false, p);
    if(!perf.completed){
        return "incomplete(perf): handled=" + std::to_string(perf.handled)
             + "/" + std::to_string(pt.produced);
    }
    pt.elapsedNs = perf.elapsedNs;
    pt.msgsPerSec = (perf.elapsedNs > 0)
        ? (static_cast<double>(pt.produced) * 1e9 / static_cast<double>(perf.elapsedNs))
        : 0.0;

    RunOutcome verif = runOnce(workers, actors, p.iterations, p.maxbatch, true, p);
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
}

std::vector<ScalePoint> sweep(const ScalingParams& p, bool sweepWorkers){
    std::vector<ScalePoint> points;
    for(int v = 1; v <= p.scaleMax; v *= 2){
        ScalePoint pt;
        pt.param = v;
        int workers = sweepWorkers ? v : p.workers;
        int actors  = sweepWorkers ? p.actors : v;
        std::string err = measurePoint(p, workers, actors, pt);
        if(!err.empty()) break;   // 호출자가 축 라벨과 함께 보고
        points.push_back(std::move(pt));
    }
    return points;
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
        runOnce(p.workers, p.actors, std::min(p.iterations, 10000), p.maxbatch, false, p);
    }

    std::vector<ScalePoint> wPts, aPts;
    for(int w = 1; w <= p.scaleMax; w *= 2){
        ScalePoint pt;
        pt.param = w;
        std::string err = measurePoint(p, w, p.actors, pt);
        if(!err.empty()) return fail("scaling failed at w=" + std::to_string(w) + ": " + err);
        wPts.push_back(std::move(pt));
    }
    for(int a = 1; a <= p.scaleMax; a *= 2){
        ScalePoint pt;
        pt.param = a;
        std::string err = measurePoint(p, p.workers, a, pt);
        if(!err.empty()) return fail("scaling failed at a=" + std::to_string(a) + ": " + err);
        aPts.push_back(std::move(pt));
    }

    // 요약은 Phase A 실측 합산만 사용 — Total Time/Throughput/Iterations 상호 일치 (#1)
    uint64_t totalMsgs = 0, totalNs = 0;
    for(auto& q : wPts){ totalMsgs += q.produced; totalNs += q.elapsedNs; }
    for(auto& q : aPts){ totalMsgs += q.produced; totalNs += q.elapsedNs; }

    BenchmarkResult res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, p.actors, p.maxbatch, 0, p.warmup};
    res.throughput.iterations = totalMsgs;
    res.throughput.totalDurationNs = totalNs;
    res.throughput.msgsPerSec = (totalNs > 0)
        ? (static_cast<double>(totalMsgs) * 1e9 / static_cast<double>(totalNs))
        : 0.0;
    res.scaling.workerScaling = std::move(wPts);
    res.scaling.actorScaling = std::move(aPts);

    V2_METRICS()->setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("scaling", [](){
        return std::make_unique<ScalingBenchmark>();
    });
    return true;
}();
