#pragma once
#include "benchmark.hpp"
#include <string>

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
    Result run(const Args& args) override;
};
