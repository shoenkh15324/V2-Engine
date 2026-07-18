#include "bench_scheduler.hpp"
#include "benchmark.hpp"
#include "core/actor_system/actor_system.hpp"
#include "core/common/time/time.hpp"
#include "core/common/time/sleep.hpp"
#include "core/actor_system/messages/tick_messages.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

SchedulerParams SchedulerParams::parse(const IBenchmark::Args& args){
    SchedulerParams p;
    for(auto& [k, v] : args){
        try{
            if(k == "workers") p.workers = std::stoi(v);
            else if(k == "maxbatch") p.maxbatch = std::stoi(v);
            else if(k == "warmup") p.warmup = std::stoi(v);
            else if(k == "interval") p.intervalMs = std::stoi(v);
            else if(k == "duration") p.durationMs = std::stoi(v);
        }catch(const std::exception&){
        }
    }
    if(p.workers < 1) p.workers = 1;
    if(p.maxbatch < 1) p.maxbatch = 1;
    if(p.warmup < 0) p.warmup = 0;
    if(p.intervalMs < 1) p.intervalMs = 1;
    if(p.durationMs < 1) p.durationMs = 1000;
    return p;
}

BenchmarkResult SchedulerBenchmark::run(const Args& args){
    bool wasMetricsEnabled = Metrics::isEnabled();
    SchedulerParams p = SchedulerParams::parse(args);

    Metrics::setEnabled(false);

    auto measureTimer = [&](int durationMs) -> std::vector<uint64_t>{
        std::vector<uint64_t> times;
        ActorSystem actorSystem(p.workers, p.maxbatch);
        auto* actor = actorSystem.createActor<TimerBenchActor>("timer_actor", 4096, times);
        actorSystem.start();

        std::thread epollThread([&](){
            actorSystem.run();
        });

        actor->startTimer(Tick{}, p.intervalMs, true);
        Sleep::sleepMs(durationMs);

        actorSystem.requestStop();
        epollThread.join();
        return times;
    };

    // Warmup: 타이머를 잠시 실행하여 epoll/커널 타이머 워밍
    if(p.warmup > 0){
        measureTimer(p.warmup);
    }

    auto fireTimes = measureTimer(p.durationMs);

    uint64_t totalFirings = fireTimes.size();
    double meanIntervalNs = 0.0;
    double minIntervalNs = 0.0;
    double maxIntervalNs = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double p999 = 0.0;

    if(totalFirings > 2){
        std::vector<double> intervals;
        intervals.reserve(totalFirings - 1);
        for(size_t i = 1; i < fireTimes.size(); i++){
            intervals.push_back(static_cast<double>(fireTimes[i] - fireTimes[i - 1]));
        }
        std::sort(intervals.begin(), intervals.end());

        double sum = 0.0;
        for(auto v : intervals) sum += v;
        meanIntervalNs = sum / static_cast<double>(intervals.size());

        minIntervalNs = intervals.front();
        maxIntervalNs = intervals.back();

        size_t n = intervals.size();
        p50  = intervals[n * 50 / 100];
        p95  = intervals[n * 95 / 100];
        p99  = intervals[n * 99 / 100];
        p999 = intervals[std::min(n * 999 / 1000, n - 1)];
    }

    BenchmarkResult res;
    res.benchmarkName = name();
    res.description = description();
    res.config = {p.workers, 0, p.maxbatch, 0, p.warmup};
    res.throughput.totalDurationNs = static_cast<uint64_t>(p.durationMs) * 1000000;
    res.scheduler.iterations = totalFirings;
    res.scheduler.avgIntervalNs = meanIntervalNs;
    res.scheduler.minIntervalNs = minIntervalNs;
    res.scheduler.maxIntervalNs = maxIntervalNs;
    res.scheduler.p50 = p50;
    res.scheduler.p95 = p95;
    res.scheduler.p99 = p99;
    res.scheduler.p999 = p999;

    Metrics::setEnabled(wasMetricsEnabled);
    return res;
}

static bool registered = []{
    Benchmark::registerBenchmark("scheduler", [](){
        return std::make_unique<SchedulerBenchmark>();
    });
    return true;
}();
