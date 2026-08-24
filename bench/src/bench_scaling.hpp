#pragma once
#include "i_benchmark.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct ScalePoint{
    int param{0};            // worker 또는 actor 수
    double msgsPerSec{0.0};  // Phase A 실측 처리량
    uint64_t elapsedNs{0};   // Phase A 실측 소요 시간
    uint64_t produced{0};    // send 시도 수 (= iterations)
    uint64_t accepted{0};    // Metrics enqueued 합 (Phase B)
    uint64_t dropped{0};     // Metrics dropped 합 (Phase B)
    uint64_t processed{0};   // Metrics processed 합 (Phase B)
    uint64_t remaining{0};   // 종료 시점 Σ mailboxCount() (Phase B)
};

struct ScalingResult{
    std::vector<ScalePoint> workerScaling;
    std::vector<ScalePoint> actorScaling;
};

struct ScalingParams{
    int actors = 16;
    int workers = 16;
    int iterations = 200000; // 충분한 측정 시간 확보 (#3)
    int maxbatch = 32;
    int warmup = 1;
    int scaleMax = 64;
    int busyStealUs = -1;    // -1이면 엔진 기본값 사용
    int idleStealUs = -1;

    static ScalingParams parse(const IBenchmark::Args& args);
};

class ScalingBenchmark : public IBenchmark{
public:
    const char* name() const override { return "scaling"; }
    const char* description() const override { return "Measure worker and actor scaling efficiency"; }
    BenchmarkResult run(const Args& args) override;
};
