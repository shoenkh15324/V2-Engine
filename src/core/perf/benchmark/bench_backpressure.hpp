#pragma once
#include "i_benchmark.hpp"
#include <string>

struct BackpressureResult{
    uint64_t sent{0};
    uint64_t dropped{0};
    double dropRate{0.0};
    uint64_t floodDurationNs{0};
    uint64_t drainDurationNs{0};
};

struct BackpressureParams{
    int workers = 4;
    int maxbatch = 32;
    int warmup = 0;
    size_t mailbox = 64;
    int floodRate = 1000; // msg / ms (total = floodRate * floodDurationMs)
    int floodDurationMs = 100;
    int floodThreads = 1;
    int mode = 0; // 0=empty_start, 1=prefill, 2=nearly_full

    static BackpressureParams parse(const IBenchmark::Args& args);
};

class BackpressureBenchmark : public IBenchmark{
public:
    const char* name() const override { return "backpressure"; }
    const char* description() const override { return "Measure mailbox overflow behavior"; }
    BenchmarkResult run(const Args& args) override;
};
