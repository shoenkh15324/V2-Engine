#pragma once
#include <string>
#include <utility>
#include <vector>

struct BenchmarkResult;

class IBenchmark{
public:
    using Args = std::vector<std::pair<std::string, std::string>>;

    virtual ~IBenchmark() = default;

    virtual const char* name() const = 0;
    virtual const char* description() const = 0;
    virtual BenchmarkResult run(const Args& args) = 0;
};
