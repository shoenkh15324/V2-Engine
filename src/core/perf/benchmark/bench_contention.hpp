#pragma once
#include "i_benchmark.hpp"
#include <string>

struct ContentionResult{
    uint64_t iterations{0};
    uint64_t totalDurationNs{0};
    double msgsPerSec{0.0};
};

struct ContentionParams{
    int workers = 4;
    int producers = 8;
    int iterations = 10000;
    int maxbatch = 32;
    int warmup = 0;
    size_t mailbox = 0;

    static ContentionParams parse(const IBenchmark::Args& args);
};

class ContentionBenchmark : public IBenchmark{
public:
    const char* name() const override { return "contention"; }
    const char* description() const override { return "Measure multi-producer contention performance"; }
    BenchmarkResult run(const Args& args) override;
};
