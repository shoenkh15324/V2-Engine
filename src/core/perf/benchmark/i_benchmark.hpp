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

        struct TestConfig{
            int workers{0};
            int actors{0};
            int maxBatch{0};
            size_t mailboxSize{0};
        };

        struct ActorSnap{
            std::string name;
            size_t mailboxCapacity{0};
            uint64_t msgProcessed{0};
        };

        TestConfig config{};
        std::vector<ActorSnap> actorSnaps;
        uint64_t iterations{0};
        uint64_t totalDurationNs{0};
        double throughputPerSec{0.0};
        double avgLatencyNs{0.0};
    };

    virtual ~IBenchmark() = default;

    virtual const char* name() const = 0;
    virtual const char* description() const = 0;

    // args: 파싱된 인자 맵 ("workers" -> "4", "iterations" -> "10000"), return: 측정 결과
    virtual Result run(const Args& args) = 0;
};
