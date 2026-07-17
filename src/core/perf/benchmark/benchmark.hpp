#pragma once
#include "i_benchmark.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

struct BenchmarkInfo{
    std::string name;
    std::string description;
};

using BenchmarkResult = IBenchmark::Result;
using BenchmarkArgs = IBenchmark::Args;
using BenchmarkCreator = std::function<std::unique_ptr<IBenchmark>()>;

class Benchmark{
public:
    Benchmark() = delete;

    Benchmark(const Benchmark&) = delete;
    Benchmark& operator=(const Benchmark&) = delete;
    Benchmark(Benchmark&&) = delete;
    Benchmark& operator=(Benchmark&&) = delete;

    static void registerBenchmark(const std::string& name, BenchmarkCreator creator);
    static std::vector<BenchmarkInfo> list();
    static BenchmarkResult run(const std::string& name, const BenchmarkArgs& args);
    static std::string runAll(const BenchmarkArgs& args);

private:
    struct RegistryEntry{
        std::string name;
        std::string description;
        BenchmarkCreator creator;
    };
    static std::vector<RegistryEntry>& entries();
};
