#include "core/perf/benchmark/benchmark.hpp"
#include "core/perf/benchmark/i_benchmark.hpp"
#include "core/common/log/log.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>

static void printResult(const IBenchmark::Result& r){
    std::cout << "=== " << r.benchmarkName << " ===\n";
    std::cout << r.description << "\n\n";

    std::cout << "[Config]\n";
    std::cout << "  Workers:    " << r.config.workers << "\n";
    std::cout << "  Actors:     " << r.config.actors << "\n";
    std::cout << "  MaxBatch:   " << r.config.maxBatch << "\n";
    std::cout << "  Mailbox:    " << r.config.mailboxSize << "\n";
    std::cout << "  Iterations: " << r.throughput.iterations << "\n";
    std::cout << "  Warmup:     " << r.config.warmup << "\n\n";

    char buf[64];

    if(r.throughput.msgsPerSec > 0.0){
        std::snprintf(buf, sizeof(buf), "%.2f", r.throughput.msgsPerSec);
        std::cout << "  Throughput: " << buf << " msgs/sec\n";
    }
    if(r.latency.avgNs > 0.0){
        std::snprintf(buf, sizeof(buf), "%.2f", r.latency.avgNs);
        std::cout << "  Avg Latency: " << buf << " ns\n";
    }
    std::snprintf(buf, sizeof(buf), "%.2f", r.throughput.totalDurationNs / 1000000.0);
    std::cout << "  Total Time: " << buf << " ms\n";

    if(r.latency.percentiles.p50 > 0.0 && r.latency.minNs > 0.0){
        std::cout << "\n[Latency Distribution]\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.latency.minNs);
        std::cout << "  Min:  " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.latency.percentiles.p50);
        std::cout << "  P50:  " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.latency.percentiles.p95);
        std::cout << "  P95:  " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.latency.percentiles.p99);
        std::cout << "  P99:  " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.latency.percentiles.p999);
        std::cout << "  P999: " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.latency.maxNs);
        std::cout << "  Max:  " << buf << " ns\n";
    }

    if(r.backpressure.sent > 0){
        std::cout << "\n[Backpressure]\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.backpressure.dropRate);
        std::cout << "  Drop Rate:   " << buf << "%\n";
        std::cout << "  Sent:        " << r.backpressure.sent << "\n";
        std::cout << "  Dropped:     " << r.backpressure.dropped << "\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.backpressure.floodDurationNs / 1000000.0);
        std::cout << "  Flood Time:  " << buf << " ms\n";
        std::snprintf(buf, sizeof(buf), "%.2f", r.backpressure.drainDurationNs / 1000000.0);
        std::cout << "  Drain Time:  " << buf << " ms\n";
    }

    if(!r.scalingCurve.workerScaling.empty()){
        std::cout << "\n[Worker Scaling]\n";
        std::cout << std::setw(8) << "Workers" << std::setw(16) << "Throughput" << std::setw(12) << "Efficiency" << "\n";
        std::cout << std::string(36, '-') << "\n";
        double baseTp = r.scalingCurve.workerScaling.front().second;
        for(auto& [w, tp] : r.scalingCurve.workerScaling){
            double eff = (baseTp > 0) ? (tp / (w * baseTp)) : 0.0;
            char buf1[32], buf2[32];
            std::snprintf(buf1, sizeof(buf1), "%.0f", tp);
            std::snprintf(buf2, sizeof(buf2), "%.2fx", eff);
            std::cout << std::setw(8) << w << std::setw(14) << std::string(buf1) + " m/s" << std::setw(12) << buf2 << "\n";
        }
        std::cout << "\n[Actor Scaling]\n";
        std::cout << std::setw(8) << "Actors" << std::setw(16) << "Throughput" << std::setw(12) << "Efficiency" << "\n";
        std::cout << std::string(36, '-') << "\n";
        baseTp = r.scalingCurve.actorScaling.front().second;
        for(auto& [a, tp] : r.scalingCurve.actorScaling){
            double eff = (baseTp > 0) ? (tp / (a * baseTp)) : 0.0;
            char buf1[32], buf2[32];
            std::snprintf(buf1, sizeof(buf1), "%.0f", tp);
            std::snprintf(buf2, sizeof(buf2), "%.2fx", eff);
            std::cout << std::setw(8) << a << std::setw(14) << std::string(buf1) + " m/s" << std::setw(12) << buf2 << "\n";
        }
    }

    if(!r.actorSnaps.empty()){
        std::cout << "\n[Actors]\n";
        for(auto& a : r.actorSnaps){
            std::cout << "  " << a.name << "  mailbox=" << a.mailboxCapacity
                      << "  processed=" << a.msgProcessed << "\n";
        }
    }
    std::cout << "\n";
}

int main(){
    setLogLevel(LogLevel::Error);

    std::cout << "=== V2 Benchmark Runner ===\n\n";

    auto list = Benchmark::list();
    std::cout << "Registered benchmarks: " << list.size() << "\n";
    for(auto& b : list) std::cout << "  " << b.name << " - " << b.description << "\n";
    std::cout << "\n";

    // 1. Throughput
    std::cout << ">>> Running throughput...\n";
    auto r1 = Benchmark::run("throughput", {{"workers", "4"}, {"iterations", "50000"}, {"actors", "4"}, {"warmup", "1000"}});
    printResult(r1);

    // 2. Latency
    std::cout << ">>> Running latency...\n";
    auto r2 = Benchmark::run("latency", {{"workers", "4"}, {"iterations", "50000"}});
    printResult(r2);

    // 3. Contention
    std::cout << ">>> Running contention (8 producers)...\n";
    auto r3 = Benchmark::run("contention", {{"workers", "4"}, {"producers", "8"}, {"iterations", "50000"}});
    printResult(r3);

    // 4. Scaling
    std::cout << ">>> Running scaling...\n";
    auto r4 = Benchmark::run("scaling", {{"iterations", "100000"}, {"scale-max", "8"}});
    printResult(r4);

    // 5. Backpressure
    std::cout << ">>> Running backpressure...\n";
    auto r5 = Benchmark::run("backpressure", {{"workers", "4"}, {"mailbox", "64"}, {"flood-rate", "1000000"}, {"flood-duration", "100"}});
    printResult(r5);

    // 6. Scheduler
    std::cout << ">>> Running scheduler...\n";
    auto r6 = Benchmark::run("scheduler", {{"workers", "4"}, {"interval", "50"}, {"duration", "5000"}});
    printResult(r6);

    return 0;
}
