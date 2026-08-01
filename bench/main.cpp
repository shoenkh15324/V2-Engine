#include "bench/benchmark.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstdio>

namespace {

void printUsage(const char* prog){
    std::cout
        << "Usage: " << prog << " <command> [--key value ...]\n"
        << "\n"
        << "Commands:\n"
        << "  list                         List available benchmarks\n"
        << "  <name> [--key value ...]     Run a single benchmark by name\n"
        << "  all [--key value ...]        Run all benchmarks\n"
        << "\n"
        << "Common options:\n"
        << "  --workers N                  Worker count\n"
        << "  --actors N                   Actor count\n"
        << "  --iterations N               Iteration count\n"
        << "  --maxbatch N                 Max batch size\n"
        << "  --warmup N                   Warmup rounds\n"
        << "  --mailbox N                  Mailbox size\n";
}

std::string formatResult(const BenchmarkResult& result){
    if(!result.success) return "error: " + result.errorMsg + "\n";

    std::ostringstream oss;
    oss << "=== Benchmark: " << result.benchmarkName << " ===\n"
        << result.description << "\n\n";
    oss << "[Test Config]\n"
        << "  Workers:    " << result.config.workers << "\n"
        << "  Actors:     " << result.config.actors << "\n"
        << "  MaxBatch:   " << result.config.maxBatch << "\n"
        << "  Mailbox:    " << result.config.mailboxSize << "\n";
    if(result.throughput.iterations > 0)
        oss << "  Iterations: " << result.throughput.iterations << "\n";
    oss << "  Warmup:     " << result.config.warmup << "\n";

    char buf[64];
    if(result.throughput.msgsPerSec > 0.0){
        std::snprintf(buf, sizeof(buf), "%.2f", result.throughput.msgsPerSec);
        oss << "\n  Throughput: " << buf << " msgs/sec\n";
    }
    if(result.latency.avgNs > 0.0){
        std::snprintf(buf, sizeof(buf), "%.2f", result.latency.avgNs);
        oss << "  Latency:    " << buf << " ns/msg\n";
    }
    if(result.throughput.totalDurationNs > 0){
        std::snprintf(buf, sizeof(buf), "%.2f", result.throughput.totalDurationNs / 1000000.0);
        oss << "  Total Time: " << buf << " ms\n";
    }

    if(result.latency.percentiles.p50 > 0.0){
        oss << "\n[Latency Distribution]\n";
        std::snprintf(buf, sizeof(buf), "%.0f", result.latency.percentiles.p50);
        oss << "  P50:  " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.0f", result.latency.percentiles.p95);
        oss << "  P95:  " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.0f", result.latency.percentiles.p99);
        oss << "  P99:  " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.0f", result.latency.percentiles.p999);
        oss << "  P999: " << buf << " ns\n";
    }

    if(result.backpressure.sent > 0){
        oss << "\n[Backpressure]\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.backpressure.dropRate);
        oss << "  Drop Rate:   " << buf << "\n";
        oss << "  Sent:        " << result.backpressure.sent << "\n";
        oss << "  Dropped:     " << result.backpressure.dropped << "\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.backpressure.floodDurationNs / 1000000.0);
        oss << "  Flood Time:  " << buf << " ms\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.backpressure.drainDurationNs / 1000000.0);
        oss << "  Drain Time:  " << buf << " ms\n";
    }

    if(result.scheduler.iterations > 0){
        oss << "\n[Scheduler]\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.scheduler.avgIntervalNs);
        oss << "  Avg Interval: " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.scheduler.minIntervalNs);
        oss << "  Min Interval: " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.scheduler.maxIntervalNs);
        oss << "  Max Interval: " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.scheduler.p50);
        oss << "  P50:          " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.scheduler.p95);
        oss << "  P95:          " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.scheduler.p99);
        oss << "  P99:          " << buf << " ns\n";
        std::snprintf(buf, sizeof(buf), "%.2f", result.scheduler.p999);
        oss << "  P999:         " << buf << " ns\n";
    }

    if(!result.scaling.workerScaling.empty()){
        oss << "\n[Worker Scaling]\n";
        double baseTp = result.scaling.workerScaling.front().second;
        for(auto& [w, tp] : result.scaling.workerScaling){
            double eff = (baseTp > 0) ? (tp / (w * baseTp)) : 0.0;
            std::snprintf(buf, sizeof(buf), "%.0f", tp);
            oss << "  " << w << " workers: " << buf << " m/s";
            std::snprintf(buf, sizeof(buf), "%.2f", eff);
            oss << " (" << buf << "x)\n";
        }
        oss << "\n[Actor Scaling]\n";
        baseTp = result.scaling.actorScaling.front().second;
        for(auto& [a, tp] : result.scaling.actorScaling){
            double eff = (baseTp > 0) ? (tp / (a * baseTp)) : 0.0;
            std::snprintf(buf, sizeof(buf), "%.0f", tp);
            oss << "  " << a << " actors: " << buf << " m/s";
            std::snprintf(buf, sizeof(buf), "%.2f", eff);
            oss << " (" << buf << "x)\n";
        }
    }

    if(!result.actorSnaps.empty()){
        oss << "\n[Actors]\n";
        oss << std::left
            << std::setw(16) << "Name"
            << std::right
            << std::setw(12) << "Mailbox"
            << std::setw(12) << "Processed"
            << "\n";
        oss << std::string(40, '-') << "\n";
        for(auto& a : result.actorSnaps){
            oss << std::left
                << std::setw(16) << a.name
                << std::right
                << std::setw(12) << a.mailboxCapacity
                << std::setw(12) << a.msgProcessed
                << "\n";
        }
    }
    return oss.str();
}

} // namespace

int main(int argc, char** argv){
    if(argc < 2){
        printUsage(argv[0]);
        return 0;
    }

    std::string cmd = argv[1];
    BenchmarkArgs args;
    for(int i = 2; i + 1 < argc; i += 2){
        std::string k = argv[i];
        std::string v = argv[i + 1];
        if(k.size() >= 2 && k.substr(0, 2) == "--"){
            args.push_back({k.substr(2), v});
        }
    }

    if(cmd == "list"){
        auto benchmarks = Benchmark::list();
        if(benchmarks.empty()){
            std::cout << "no benchmarks registered\n";
            return 0;
        }
        std::cout << "Available benchmarks:\n";
        for(auto& b : benchmarks){
            std::cout << "  " << std::left << std::setw(16) << b.name << b.description << "\n";
        }
        return 0;
    }

    if(cmd == "all"){
        std::cout << Benchmark::runAll(args);
        return 0;
    }

    auto result = Benchmark::run(cmd, args);
    std::cout << formatResult(std::move(result));
    return 0;
}
