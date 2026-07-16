#include "benchmark.hpp"
#include <sstream>
#include <iomanip>

void Benchmark::registerBenchmark(const std::string& name, BenchmarkCreator creator){
    registry()[name] = std::move(creator);
}

std::vector<BenchmarkInfo> Benchmark::list(){
    std::vector<BenchmarkInfo> result;
    for(auto& [name, creator] : registry()){
        auto bench = creator();
        result.push_back({bench->name(), bench->description()});
    }
    return result;
}

BenchmarkResult Benchmark::run(const std::string& name, const BenchmarkArgs& args){
    auto& reg = registry();
    auto it = reg.find(name);
    if(it == reg.end()){
        BenchmarkResult err;
        err.success = false;
        err.errorMsg = "Unknown benchmark '" + name + "'";
        return err;
    }
    auto bench = it->second();
    return bench->run(args);
}

std::string Benchmark::runAll(const BenchmarkArgs& args){
    auto& reg = registry();
    if(reg.empty()) return "error: no benchmarks registered\n";

    std::ostringstream oss;
    oss << "=== Benchmark Summary ===\n\n";
    oss << std::left << std::setw(16) << "Benchmark"
        << std::right << std::setw(16) << "Throughput"
        << std::setw(14) << "Latency"
        << std::setw(12) << "Time(ms)"
        << "\n";
    oss << std::string(58, '-') << "\n";

    for(auto& [name, creator] : reg){
        auto bench = creator();
        auto result = bench->run(args);

        oss << std::left << std::setw(16) << result.benchmarkName;
        if(result.success){
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.2f", result.throughputPerSec);
            oss << std::right << std::setw(16) << std::string(buf) + " m/s";
            std::snprintf(buf, sizeof(buf), "%.2f", result.avgLatencyNs);
            oss << std::setw(14) << std::string(buf) + " ns";
            std::snprintf(buf, sizeof(buf), "%.2f", result.totalDurationNs / 1000000.0);
            oss << std::setw(12) << buf << "\n";
        }else{
            oss << std::setw(16) << " "
                << std::setw(14) << " "
                << "error: " << result.errorMsg << "\n";
        }
    }
    return oss.str();
}

std::map<std::string, BenchmarkCreator>& Benchmark::registry(){
    static std::map<std::string, BenchmarkCreator> reg;
    return reg;
}
