#include "benchmark.hpp"
#include <sstream>
#include <iomanip>

void Benchmark::registerBenchmark(const std::string& name, BenchmarkCreator creator){
    auto bench = creator();
    entries().push_back({bench->name(), bench->description(), std::move(creator)});
}

std::vector<BenchmarkInfo> Benchmark::list(){
    std::vector<BenchmarkInfo> result;
    for(auto& e : entries()){
        result.push_back({e.name, e.description});
    }
    return result;
}

BenchmarkResult Benchmark::run(const std::string& name, const BenchmarkArgs& args){
    for(auto& e : entries()){
        if(e.name == name){
            auto bench = e.creator();
            return bench->run(args);
        }
    }
    BenchmarkResult err;
    err.success = false;
    err.errorMsg = "Unknown benchmark '" + name + "'";
    return err;
}

std::string Benchmark::runAll(const BenchmarkArgs& args){
    if(entries().empty()) return "error: no benchmarks registered\n";

    std::ostringstream oss;
    oss << "=== Benchmark Summary ===\n\n";

    for(auto& e : entries()){
        auto bench = e.creator();
        auto result = bench->run(args);

        oss << "=== " << result.benchmarkName << " ===\n";
        if(result.success){
            char buf[64];
            if(result.throughput.msgsPerSec > 0.0){
                std::snprintf(buf, sizeof(buf), "%.2f", result.throughput.msgsPerSec);
                oss << "  Throughput: " << buf << " msgs/sec\n";
            }
            if(result.latency.avgNs > 0.0){
                std::snprintf(buf, sizeof(buf), "%.2f", result.latency.avgNs);
                oss << "  Avg Latency: " << buf << " ns\n";
            }
            if(result.backpressure.sent > 0){
                std::snprintf(buf, sizeof(buf), "%.2f", result.backpressure.dropRate);
                oss << "  Drop Rate: " << buf << "%\n";
                oss << "  Sent: " << result.backpressure.sent
                    << "  Dropped: " << result.backpressure.dropped << "\n";
            }
            if(result.throughput.totalDurationNs > 0){
                std::snprintf(buf, sizeof(buf), "%.2f", result.throughput.totalDurationNs / 1000000.0);
                oss << "  Total Time: " << buf << " ms\n";
            }
        }else{
            oss << "  error: " << result.errorMsg << "\n";
        }
        oss << "\n";
    }
    return oss.str();
}

std::vector<Benchmark::RegistryEntry>& Benchmark::entries(){
    static std::vector<RegistryEntry> reg;
    return reg;
}
