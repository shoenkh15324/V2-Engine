#pragma once
#include "benchmark.hpp"
#include <string>

struct BackpressureParams{
    int workers = 4;
    int maxbatch = 32;
    int warmup = 0;
    size_t mailbox = 64;
    int floodRate = 1000000;
    int floodDurationMs = 100;

    static BackpressureParams parse(const IBenchmark::Args& args);
};

class BackpressureBenchmark : public IBenchmark{
public:
    const char* name() const override { return "backpressure"; }
    const char* description() const override { return "Measure mailbox overflow behavior"; }
    Result run(const Args& args) override;
};
