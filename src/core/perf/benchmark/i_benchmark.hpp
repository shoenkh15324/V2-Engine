#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class IBenchmark{
public:
    using Args = std::vector<std::pair<std::string, std::string>>;

    struct Result{
        std::string benchmarkName;
        std::string description;

        bool success{true};
        std::string errorMsg;

        struct Config{
            int workers{0};
            int actors{0};
            int maxBatch{0};
            size_t mailboxSize{0};
            int warmup{0};
        };

        struct ActorSnap{
            std::string name;
            size_t mailboxCapacity{0};
            uint64_t msgProcessed{0};
        };

        struct Throughput{
            uint64_t iterations{0};
            uint64_t totalDurationNs{0};
            double msgsPerSec{0.0};
        };

        struct Latency{
            double avgNs{0.0};
            double minNs{0.0};
            double maxNs{0.0};

            struct Percentiles{
                double p50{0.0};
                double p95{0.0};
                double p99{0.0};
                double p999{0.0};
            };
            Percentiles percentiles{};
        };

        struct Backpressure{
            uint64_t sent{0};
            uint64_t dropped{0};
            double dropRate{0.0};
            uint64_t floodDurationNs{0};
            uint64_t drainDurationNs{0};
        };

        struct ScalingCurve{
            std::vector<std::pair<int, double>> workerScaling;
            std::vector<std::pair<int, double>> actorScaling;
        };

        Config config{};
        std::vector<ActorSnap> actorSnaps;
        Throughput throughput{};
        Latency latency{};
        Backpressure backpressure{};
        ScalingCurve scalingCurve{};
    };

    virtual ~IBenchmark() = default;

    virtual const char* name() const = 0;
    virtual const char* description() const = 0;

    // args: 파싱된 인자 맵 ("workers" -> "4", "iterations" -> "10000"), return: 측정 결과
    virtual Result run(const Args& args) = 0;
};
