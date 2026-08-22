#include "bench/benchmark.hpp"
#include <ctime>
#include <string>
#include <vector>
#include <cstdio>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <filesystem>

#ifndef V2_BENCH_RESULTS_DIR
    #define V2_BENCH_RESULTS_DIR "bench/results"
#endif

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
        oss << "  Backlog:     " << result.backpressure.backlogAtDrainStart << "\n";
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

    if(!result.scaling.workerScaling.empty() || !result.scaling.actorScaling.empty()){
        auto printSeries = [&](const char* label, const std::vector<ScalePoint>& pts){
            if(pts.empty()) return;
            double baseTp = pts.front().msgsPerSec;
            oss << "\n[" << label << "] (baseline = " << pts.front().param << ")\n";
            for(auto& q : pts){
                double speedup = (baseTp > 0) ? (q.msgsPerSec / baseTp) : 0.0;
                double eff = speedup / static_cast<double>(q.param);
                std::snprintf(buf, sizeof(buf), "%.0f", q.msgsPerSec);
                oss << "  " << std::setw(4) << q.param << " " << label << ": "
                    << std::right << std::setw(12) << buf << " m/s";
                std::snprintf(buf, sizeof(buf), "%.2fx", speedup);
                oss << " | speedup " << buf;
                std::snprintf(buf, sizeof(buf), "%.1f%%", eff * 100.0);
                oss << " | eff " << buf << "\n";
            }
        };
        printSeries("workers", result.scaling.workerScaling);
        printSeries("actors", result.scaling.actorScaling);

        oss << "\n[Verification] (P=Produced A=Accepted D=Dropped Pr=Processed R=Remaining)\n";
        auto printVerif = [&](char tag, const ScalePoint& q){
            bool ok = (q.produced == q.accepted + q.dropped)
                   && (q.accepted == q.processed + q.remaining)
                   && (q.remaining == 0);
            oss << "  " << tag << "=" << std::left << std::setw(6) << q.param << std::right
                << "P=" << q.produced
                << " A=" << q.accepted
                << " D=" << q.dropped
                << " Pr=" << q.processed
                << " R=" << q.remaining
                << (ok ? "  OK" : "  FAIL") << "\n";
        };
        for(auto& q : result.scaling.workerScaling) printVerif('w', q);
        for(auto& q : result.scaling.actorScaling) printVerif('a', q);
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

std::tm localTime(std::time_t t){
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string timestamp(){
    auto tp = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = localTime(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

std::string defaultOutputPath(const std::string& benchName){
    return (std::filesystem::path(V2_BENCH_RESULTS_DIR) / (benchName + "_" + timestamp() + ".txt")).string();

}

std::string extractArg(const IBenchmark::Args& args, const std::string& key){
    for(auto& [k, v] : args){
        if(k == key) return v;
    }
    return "";
}

std::string resolveOutputPath(const BenchmarkArgs& args, const std::string& benchName){
    std::string out = extractArg(args, "output");
    std::string fname = benchName + "_" + timestamp() + ".txt";

    if(out.empty()) return defaultOutputPath(benchName);
    std::filesystem::path p(out);
    if(std::filesystem::is_directory(p) || (out.back() == '/')){
        return (p / fname).string();
    }
    return p.string();
}

void saveResult(const std::string& path, const std::string& content){
    std::error_code ec;
    auto parent = std::filesystem::path(path).parent_path();
    if(!parent.empty()){
        std::filesystem::create_directories(parent, ec);
        if(ec){
            std::cerr << "warn: failed to create directory '" << parent.string()
                      << "': " << ec.message() << "\n";
            return;
        }
    }

    std::ofstream f(path);
    if(!f.is_open()){
        std::cerr << "warn: failed to write result file: " << path << "\n";
        return;
    }
    f << content;
    std::cout << "[saved] " << path << "\n";
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
        auto results = Benchmark::runAll(args);
        std::ostringstream combined;
        for(size_t i = 0; i < results.size(); ++i){
            combined << formatResult(results[i]);
            if(i + 1 < results.size()){
                combined << "\n";
            }
        }
        std::string content = combined.str();
        std::cout << content;
        saveResult(resolveOutputPath(args, "all"), content);
        return 0;
    }

    auto result = Benchmark::run(cmd, args);
    std::string content = formatResult(result);
    std::cout << content;
    if(result.success){
        saveResult(resolveOutputPath(args, cmd), content);
    }
    return 0;
}
