#pragma once
#include <string>
#include <vector>
#include <cstdint>

class IBenchmark{
public:
    using Args = std::vector<std::pair<std::string, std::string>>;

    struct Result{
        std::string benchmarkName;
        std::string description;
        
        bool success{true};
        std::string errorMsg;

        uint64_t iterations{0};
        uint64_t totalDurationNs{0};
        double throughputPerSec{0.0};
        double avgLatencyNs{0.0};

        struct WorkerSnap{
            int workerId;
            uint64_t msgProcessed{0};
            uint64_t busyNs{0};
            uint64_t idleNs{0};
        };

        struct DispatcherSnap{
            uint64_t dispatchCount{0};
            uint64_t acquireCount{0};
            uint64_t deduplicated{0};
            size_t readyQueuePeak{0};
        };
        
        std::vector<WorkerSnap> workers;
        DispatcherSnap dispatcher{};
    };

    virtual ~IBenchmark() = default;

    virtual const char* name() const = 0;
    virtual const char* description() const = 0;

    // args: 파싱된 인자 맵 ("workers" -> "4", "iterations" -> "10000"), return: 측정 결과
    virtual Result run(const Args& args) = 0;
};
