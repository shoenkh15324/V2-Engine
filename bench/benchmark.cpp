#include "benchmark.hpp"

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

std::vector<BenchmarkResult> Benchmark::runAll(const BenchmarkArgs& args){
    std::vector<BenchmarkResult> results;
    results.reserve(entries().size());
    for(auto& e : entries()){
        auto bench = e.creator();
        results.push_back(bench->run(args));
    }
    return results;
}

std::vector<Benchmark::RegistryEntry>& Benchmark::entries(){
    static std::vector<RegistryEntry> reg;
    return reg;
}
