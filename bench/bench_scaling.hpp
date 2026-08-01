#pragma once
#include "i_benchmark.hpp"
#include <string>
#include <utility>
#include <vector>

struct ScalingResult{
    std::vector<std::pair<int, double>> workerScaling;
    std::vector<std::pair<int, double>> actorScaling;
};

struct ScalingParams{
    int actors = 16;
    int workers = 16;
    int iterations = 10000;
    int maxbatch = 32;
    int warmup = 0;
    int scaleMax = 64;

    static ScalingParams parse(const IBenchmark::Args& args);
};

class ScalingBenchmark : public IBenchmark{
public:
    const char* name() const override { return "scaling"; }
    const char* description() const override { return "Measure worker and actor scaling efficiency"; }
    BenchmarkResult run(const Args& args) override;
};
