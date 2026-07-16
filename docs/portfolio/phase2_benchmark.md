# Phase 2: Benchmark Framework Completion

> **Duration**: Week 4-8 (3-4 weeks, overlaps with Phase 1)
> **Goal**: Complete all empty benchmark scaffolds + add mailbox comparison benchmark
> **Prerequisite**: Phase 1 (IMailbox interface and at least MutexMailbox working)

---

## Table of Contents

1. [Objectives](#1-objectives)
2. [Current Benchmark Framework](#2-current-benchmark-framework)
3. [Benchmark Design: Latency](#3-benchmark-design-latency)
4. [Benchmark Design: Contention](#4-benchmark-design-contention)
5. [Benchmark Design: Scaling](#5-benchmark-design-scaling)
6. [Benchmark Design: Backpressure](#6-benchmark-design-backpressure)
7. [Benchmark Design: Scheduler](#7-benchmark-design-scheduler)
8. [Benchmark Design: Mailbox Comparison](#8-benchmark-design-mailbox-comparison)
9. [CLI Integration](#9-cli-integration)
10. [Implementation Steps](#10-implementation-steps)
11. [Verification Checklist](#11-verification-checklist)

---

## 1. Objectives

### 1.1 Primary Objectives

1. Implement 5 empty benchmark scaffolds (latency, contention, scaling, backpressure, scheduler)
2. Implement mailbox comparison benchmark (Mutex vs MPSC)
3. Add JSON output format for benchmark results
4. Add `--compare` flag for side-by-side execution
5. Ensure all benchmarks integrate with existing CLI (`v2 benchmark`)

### 1.2 Benchmark Coverage Matrix

| Benchmark | What It Measures | Key Metrics | Configurations |
|-----------|------------------|-------------|----------------|
| **throughput** | Messages per second | msgs/sec, ns/msg | workers, actors, maxbatch, mailbox |
| **latency** | Per-message latency | avg, p50, p95, p99, p999 | workers, actors, mailbox |
| **contention** | Performance under contention | msgs/sec vs producers | num_producers, mailbox |
| **scaling** | Scaling efficiency | msgs/sec vs workers/actors | worker_range, actor_range |
| **backpressure** | Overflow behavior | drop_rate, recovery_time | mailbox_size, fill_rate |
| **scheduler** | Timer precision | timer_accuracy, overhead | num_timers, interval |
| **comparison** | Mailbox variant comparison | all_metrics | mailbox types, configurations |

---

## 2. Current Benchmark Framework

### 2.1 Existing Infrastructure

**File**: `src/core/perf/benchmark/i_benchmark.hpp`

```cpp
class IBenchmark {
public:
    virtual ~IBenchmark() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual Result run(const Args& args) = 0;
};

struct Result {
    std::string benchmarkName;
    std::string description;
    Config config;  // workers, actors, maxBatch, mailboxSize
    int iterations;
    uint64_t totalDurationNs;
    double throughputPerSec;
    double avgLatencyNs;
    std::vector<ActorSnap> actorSnaps;
};
```

**File**: `src/core/perf/benchmark/benchmark.cpp`

- `Benchmark::registerBenchmark(name, creator)` - self-registration
- `Benchmark::list()` - returns all registered benchmark names
- `Benchmark::run(name, args)` - instantiates and runs benchmark
- `Benchmark::runAll()` - runs all benchmarks with summary

### 2.2 Existing Throughput Benchmark

**File**: `src/core/perf/benchmark/bench_throughput.cpp`

```cpp
IBenchmark::Result ThroughputBenchmark::run(const Args& args) {
    // 1. Parse args (workers, actors, iterations, maxbatch)
    // 2. Create ActorSystem with workers and maxbatch
    // 3. Create N BenchActors
    // 4. Start system
    // 5. Send iterations messages round-robin
    // 6. Wait for all messages processed (spin-wait with sleep)
    // 7. Stop system
    // 8. Calculate throughput and latency
    // 9. Return Result
}
```

### 2.3 BenchActor

**File**: `src/core/perf/benchmark/bench_throughput.hpp`

```cpp
class BenchActor : public Actor {
public:
    void handle(const Message& msg) override {
        counter_.fetch_add(1, std::memory_order_relaxed);
        processed_.fetch_add(1, std::memory_order_relaxed);
    }
    // ...
};
```

**Design**: Minimal actor that only increments atomics. Measures system overhead, not handler logic.

---

## 3. Benchmark Design: Latency

### 3.1 Purpose

Measure per-message end-to-end latency: time from `enqueue()` to `handle()` completion.

### 3.2 Design

**Approach**: Ping-pong pattern
1. Producer sends message with timestamp
2. Consumer (handler) records receive timestamp
3. Calculate latency = receive - send
4. Repeat for N messages, report statistics

**Challenge**: Clock synchronization between threads. Use `std::chrono::steady_clock` (monotonic, thread-safe).

### 3.3 Implementation Plan

**File**: `src/core/perf/benchmark/bench_latency.cpp`

```cpp
struct LatencyBenchActor : public Actor {
    std::vector<uint64_t>& latencies;
    std::atomic<uint64_t>& sendTimestamp;

    void handle(const Message& msg) override {
        uint64_t receiveTime = Time::nowNs();
        uint64_t sendTime = sendTimestamp.load(std::memory_order_relaxed);
        latencies.push_back(receiveTime - sendTime);
    }
};

IBenchmark::Result LatencyBenchmark::run(const Args& args) {
    // 1. Parse args
    // 2. Create ActorSystem
    // 3. Create single LatencyBenchActor
    // 4. Start system
    // 5. For each iteration:
    //    a. Record send timestamp
    //    b. Send message
    //    c. Wait for handler to process (spin-wait on atomic)
    // 6. Stop system
    // 7. Calculate statistics (avg, p50, p95, p99, p999)
    // 8. Return Result
}
```

### 3.4 Result Extension

Extend `IBenchmark::Result` to include latency distribution:

```cpp
struct Result {
    // ... existing fields ...

    // Latency distribution (new)
    double p50LatencyNs;
    double p95LatencyNs;
    double p99LatencyNs;
    double p999LatencyNs;
    double minLatencyNs;
    double maxLatencyNs;
    std::vector<double> latencyHistogram;  // Optional
};
```

### 3.5 Statistics Calculation

```cpp
std::sort(latencies.begin(), latencies.end());
size_t n = latencies.size();

result.avgLatencyNs = std::accumulate(latencies.begin(), latencies.end(), 0.0) / n;
result.p50LatencyNs = latencies[n * 50 / 100];
result.p95LatencyNs = latencies[n * 95 / 100];
result.p99LatencyNs = latencies[n * 99 / 100];
result.p999LatencyNs = latencies[n * 999 / 1000];
result.minLatencyNs = latencies.front();
result.maxLatencyNs = latencies.back();
```

### 3.6 CLI Usage

```
v2 benchmark latency --actors 1 --workers 4 --iterations 100000 --mailbox mutex
v2 benchmark latency --actors 1 --workers 4 --iterations 100000 --mailbox mpsc
```

### 3.7 Expected Output

```
=== Benchmark: latency ===
Measure per-message end-to-end latency

[Test Config]
  Workers:    4
  Actors:     1
  Mailbox:    mpsc
  Iterations: 100000

[Latency Statistics]
  Average:  1234.56 ns
  P50:      1100.00 ns
  P95:      1800.00 ns
  P99:      2500.00 ns
  P999:     5000.00 ns
  Min:      800.00 ns
  Max:      15000.00 ns

  Total Time: 1234.56 ms
```

---

## 4. Benchmark Design: Contention

### 4.1 Purpose

Measure performance degradation under high contention when multiple producers target the same actor.

### 4.2 Design

**Approach**: Fixed consumer, varying producers
1. Create 1 actor
2. Spawn N producer threads
3. Each producer sends M messages
4. Measure total throughput
5. Vary N from 1 to 64

### 4.3 Implementation Plan

**File**: `src/core/perf/benchmark/bench_contention.cpp`

```cpp
IBenchmark::Result ContentionBenchmark::run(const Args& args) {
    // 1. Parse args (num_producers, msgs_per_producer, mailbox)
    // 2. Create ActorSystem
    // 3. Create single BenchActor
    // 4. Start system
    // 5. Spawn num_producers threads
    // 6. Each thread sends msgs_per_producer messages
    // 7. Wait for all threads to finish
    // 8. Wait for all messages processed
    // 9. Stop system
    // 10. Calculate throughput per producer count
    // 11. Return Result
}
```

### 4.4 Result Extension

```cpp
struct Result {
    // ... existing fields ...

    // Contention analysis (new)
    std::vector<ContentionDataPoint> contentionCurve;
};

struct ContentionDataPoint {
    int numProducers;
    double throughputPerSec;
    double avgLatencyNs;
    double contentionRatio;  // throughput / single-producer throughput
};
```

### 4.5 Expected Output

```
=== Benchmark: contention ===
Measure performance under multi-producer contention

[Test Config]
  Actors:     1
  MaxBatch:   32
  Mailbox:    mpsc
  Msgs/Producer: 10000

[Contention Curve]
Producers  Throughput     Latency     Ratio
------------------------------------------------
        1    50000.00     20.00 ns    1.00x
        2    45000.00     22.22 ns    0.90x
        4    40000.00     25.00 ns    0.80x
        8    35000.00     28.57 ns    0.70x
       16    30000.00     33.33 ns    0.60x
       32    25000.00     40.00 ns    0.50x
       64    20000.00     50.00 ns    0.40x
```

### 4.6 Key Insight for Portfolio

This benchmark demonstrates the **scalability limit** of different mailbox implementations:
- MutexMailbox: throughput degrades linearly with producers (mutex contention)
- MPSCMailbox: throughput remains stable until cache-line bouncing kicks in

---

## 5. Benchmark Design: Scaling

### 5.1 Purpose

Measure how throughput scales with number of workers and actors.

### 5.2 Design

**Two-dimensional scaling**:
1. **Worker scaling**: Fix actors, vary workers (1, 2, 4, 8, 16, 32, 64)
2. **Actor scaling**: Fix workers, vary actors (1, 2, 4, 8, 16, 32, 64)

### 5.3 Implementation Plan

**File**: `src/core/perf/benchmark/bench_scaling.cpp`

```cpp
IBenchmark::Result ScalingBenchmark::run(const Args& args) {
    // 1. Parse args (iterations, maxbatch, mailbox)
    // 2. Worker scaling:
    //    For each worker_count in [1, 2, 4, 8, 16, 32, 64]:
    //      Create ActorSystem with worker_count workers
    //      Create actors (fixed at 16)
    //      Run throughput benchmark
    //      Record result
    // 3. Actor scaling:
    //    For each actor_count in [1, 2, 4, 8, 16, 32, 64]:
    //      Create ActorSystem with fixed 16 workers
    //      Create actor_count actors
    //      Run throughput benchmark
    //      Record result
    // 4. Return combined results
}
```

### 5.4 Result Extension

```cpp
struct Result {
    // ... existing fields ...

    // Scaling analysis (new)
    std::vector<ScalingDataPoint> workerScaling;
    std::vector<ScalingDataPoint> actorScaling;
};

struct ScalingDataPoint {
    int count;  // worker or actor count
    double throughputPerSec;
    double efficiency;  // throughput / (count * single_unit_throughput)
};
```

### 5.5 Expected Output

```
=== Benchmark: scaling ===
Measure throughput scaling with workers and actors

[Test Config]
  Iterations: 100000
  MaxBatch:   32
  Mailbox:    mpsc

[Worker Scaling] (fixed actors=16)
Workers  Throughput     Efficiency
----------------------------------------
      1     8000.00       1.00x
      2    15000.00       0.94x
      4    28000.00       0.88x
      8    48000.00       0.75x
     16    72000.00       0.56x
     32    85000.00       0.33x
     64    90000.00       0.18x

[Actor Scaling] (fixed workers=16)
Actors   Throughput     Efficiency
----------------------------------------
      1     5000.00       1.00x
      2     9800.00       0.98x
      4    19000.00       0.95x
      8    36000.00       0.90x
     16    68000.00       0.85x
     32    95000.00       0.74x
     64    110000.00      0.54x
```

### 5.6 Key Insight for Portfolio

Demonstrates **Amdahl's Law** in actor systems:
- Worker scaling plateaus due to contention on shared dispatcher
- Actor scaling improves throughput by reducing per-actor contention
- Sweet spot depends on mailbox implementation and hardware

---

## 6. Benchmark Design: Backpressure

### 6.1 Purpose

Measure behavior when mailbox is full: drop rate, recovery time, system stability.

### 6.2 Design

**Approach**: Flood and recover
1. Create actor with small mailbox (e.g., capacity=64)
2. Send messages faster than consumer can process
3. Measure drop rate
4. Stop flooding
5. Measure recovery time (time until mailbox drains)

### 6.3 Implementation Plan

**File**: `src/core/perf/benchmark/bench_backpressure.cpp`

```cpp
IBenchmark::Result BackpressureBenchmark::run(const Args& args) {
    // 1. Parse args (mailbox_size, flood_rate, flood_duration_ms)
    // 2. Create ActorSystem
    // 3. Create single BenchActor with small mailbox
    // 4. Start system
    // 5. Flood phase:
    //    - Send messages at flood_rate for flood_duration_ms
    //    - Count drops (push returns false)
    // 6. Recovery phase:
    //    - Stop sending
    //    - Measure time until mailbox empty
    // 7. Stop system
    // 8. Calculate drop rate and recovery time
    // 9. Return Result
}
```

### 6.4 Result Extension

```cpp
struct Result {
    // ... existing fields ...

    // Backpressure analysis (new)
    size_t totalSent;
    size_t totalDropped;
    double dropRate;
    uint64_t recoveryTimeNs;
    size_t peakMailboxDepth;
};
```

### 6.5 Expected Output

```
=== Benchmark: backpressure ===
Measure mailbox overflow behavior

[Test Config]
  Mailbox Size:   64
  Flood Rate:     1000000 msgs/sec
  Flood Duration: 100 ms

[Results]
  Total Sent:     100000
  Total Dropped:  85000
  Drop Rate:      85.00%
  Recovery Time:  45.23 ms
  Peak Depth:     64

[Analysis]
  The mailbox dropped 85% of messages during flood.
  Recovery took 45.23 ms to drain remaining messages.
  Consider increasing mailbox size or reducing producer rate.
```

### 6.6 Key Insight for Portfolio

Demonstrates **flow control** in actor systems:
- Shows the importance of mailbox sizing
- Compares how different mailbox variants handle overflow
- Provides data for capacity planning

---

## 7. Benchmark Design: Scheduler

### 7.1 Purpose

Measure timer scheduling precision and overhead.

### 7.2 Design

**Approach**: Timer accuracy test
1. Create N timers with known intervals
2. Measure actual firing times
3. Calculate accuracy (actual - expected)
4. Measure overhead (timer creation + cancellation cost)

### 7.3 Implementation Plan

**File**: `src/core/perf/benchmark/bench_scheduler.cpp`

```cpp
IBenchmark::Result SchedulerBenchmark::run(const Args& args) {
    // 1. Parse args (num_timers, interval_ms, duration_ms)
    // 2. Create ActorSystem
    // 3. Create TimerBenchActor that records firing times
    // 4. Start system
    // 5. Create num_timers timers with interval_ms
    // 6. Run for duration_ms
    // 7. Collect firing times
    // 8. Stop system
    // 9. Calculate accuracy statistics
    // 10. Return Result
}
```

### 7.4 Result Extension

```cpp
struct Result {
    // ... existing fields ...

    // Scheduler analysis (new)
    double timerAccuracyMeanNs;
    double timerAccuracyStdDevNs;
    uint64_t timerOverheadNs;  // time to create + cancel one timer
    size_t totalFirings;
};
```

### 7.5 Expected Output

```
=== Benchmark: scheduler ===
Measure timer scheduling precision

[Test Config]
  Timers:    16
  Interval:  100 ms
  Duration:  5000 ms

[Results]
  Total Firings:    800
  Accuracy Mean:    125.34 us
  Accuracy StdDev:  45.67 us
  Timer Overhead:   2.45 us

[Analysis]
  Timer accuracy is within expected range for Linux timerfd.
  Overhead is dominated by epoll_wait wakeup latency.
```

---

## 8. Benchmark Design: Mailbox Comparison

### 8.1 Purpose

Directly compare MutexMailbox vs MPSCMailbox under identical conditions.

### 8.2 Design

**Approach**: Run same benchmark with different mailbox types, report side-by-side

### 8.3 Implementation Plan

**File**: `src/core/perf/benchmark/bench_mailbox_comparison.cpp`

```cpp
IBenchmark::Result MailboxComparisonBenchmark::run(const Args& args) {
    // 1. Parse args
    // 2. For each MailboxType in [Mutex, MPSC]:
    //    a. Create ActorSystem with mailbox type
    //    b. Create actors
    //    c. Run throughput benchmark
    //    d. Record result
    //    e. Run latency benchmark (optional)
    //    f. Record result
    // 3. Calculate speedup ratios
    // 4. Return combined results
}
```

### 8.4 Result Structure

```cpp
struct Result {
    // ... existing fields ...

    // Comparison results (new)
    std::vector<MailboxResult> variants;
};

struct MailboxResult {
    MailboxType type;
    double throughputPerSec;
    double avgLatencyNs;
    double p99LatencyNs;
};
```

### 8.5 Expected Output

```
=== Benchmark: comparison ===
Compare mailbox implementations

[Test Config]
  Workers:    16
  Actors:     16
  MaxBatch:   32
  Iterations: 100000

[Results]
Mailbox   Throughput     Avg Latency    P99 Latency    Speedup
----------------------------------------------------------------
  Mutex    4874.27        205159.01 ns   450000.00 ns   1.00x
  MPSC    12500.00         80000.00 ns   150000.00 ns   2.56x

[Analysis]
  MPSCMailbox shows 2.56x throughput improvement.
  P99 latency reduced by 66.67%.
  Lock-free design eliminates mutex contention.
```

### 8.6 Parametric Configurations

The comparison benchmark should support multiple configurations:

```
v2 benchmark comparison --workers 4 --actors 4    # Low contention
v2 benchmark comparison --workers 16 --actors 16  # Balanced
v2 benchmark comparison --workers 64 --actors 16  # High contention
v2 benchmark comparison --workers 16 --actors 64  # Many actors
```

---

## 9. CLI Integration

### 9.1 Extended CLI Commands

**File**: `src/app/cli/cli_app.cpp`

```cpp
// Add to subCmds_ list:
{"latency", "[--actors N] [--workers N] [--iterations N] [--mailbox type]", "Measure per-message latency"},
{"contention", "[--producers N] [--msgs N] [--mailbox type]", "Measure contention performance"},
{"scaling", "[--iterations N] [--mailbox type]", "Measure scaling efficiency"},
{"backpressure", "[--mailbox-size N] [--flood-rate N] [--mailbox type]", "Measure overflow behavior"},
{"scheduler", "[--timers N] [--interval N] [--duration N]", "Measure timer precision"},
{"comparison", "[--workers N] [--actors N] [--iterations N]", "Compare mailbox implementations"},
```

### 9.2 Shared Arguments

All benchmarks accept these common arguments:

| Argument | Description | Default |
|----------|-------------|---------|
| `--workers N` | Number of worker threads | 4 |
| `--actors N` | Number of actors | 16 |
| `--iterations N` | Number of messages | 10000 |
| `--maxbatch N` | Maximum batch size | 32 |
| `--mailbox type` | Mailbox type (mutex/mpsc) | mutex |

### 9.3 JSON Output

Add `--json` flag to all benchmarks:

```
v2 benchmark throughput --actors 16 --workers 64 --json
```

Output:
```json
{
  "benchmark": "throughput",
  "config": {
    "workers": 64,
    "actors": 16,
    "maxBatch": 128,
    "mailboxSize": 6506,
    "mailboxType": "mpsc"
  },
  "iterations": 100000,
  "results": {
    "throughputPerSec": 12500.00,
    "avgLatencyNs": 80000.00,
    "totalDurationNs": 8000000000
  },
  "actorSnapshots": [...]
}
```

### 9.4 Implementation

Extend `IBenchmark::Result` with JSON serialization:

```cpp
// In i_benchmark.hpp
nlohmann::json toJson() const {
    return {
        {"benchmark", benchmarkName},
        {"config", {
            {"workers", config.workers},
            {"actors", config.actors},
            {"maxBatch", config.maxBatch},
            {"mailboxSize", config.mailboxSize}
        }},
        {"iterations", iterations},
        {"results", {
            {"throughputPerSec", throughputPerSec},
            {"avgLatencyNs", avgLatencyNs},
            {"totalDurationNs", totalDurationNs}
        }},
        {"actorSnapshots", actorSnapsJson}
    };
}
```

---

## 10. Implementation Steps

### Week 4-5: Core Benchmarks

| Day | Task | Deliverable |
|-----|------|-------------|
| 1-2 | Implement `bench_latency.cpp` | Latency benchmark |
| 3-4 | Implement `bench_contention.cpp` | Contention benchmark |
| 5-6 | Implement `bench_scaling.cpp` | Scaling benchmark |
| 7 | Test and debug all three | Working benchmarks |

### Week 5-6: Remaining Benchmarks

| Day | Task | Deliverable |
|-----|------|-------------|
| 8-9 | Implement `bench_backpressure.cpp` | Backpressure benchmark |
| 10-11 | Implement `bench_scheduler.cpp` | Scheduler benchmark |
| 12-13 | Implement `bench_mailbox_comparison.cpp` | Comparison benchmark |
| 14 | Test and debug all three | Working benchmarks |

### Week 6-7: CLI and Output

| Day | Task | Deliverable |
|-----|------|-------------|
| 15-16 | Add `--mailbox` flag to all benchmarks | Updated CLI |
| 17-18 | Add `--json` output format | JSON output |
| 19-20 | Update `IBenchmark::Result` with new fields | Extended results |
| 21 | Test CLI integration | Working CLI |

### Week 7-8: Polish

| Day | Task | Deliverable |
|-----|------|-------------|
| 22-23 | Add benchmark descriptions and help text | User-friendly output |
| 24-25 | Performance optimization | Optimized benchmarks |
| 26-27 | Documentation updates | Updated docs |
| 28 | Final testing | All benchmarks working |

---

## 11. Verification Checklist

### 11.1 Functionality

- [ ] `bench_latency` produces correct latency statistics
- [ ] `bench_contention` produces contention curve
- [ ] `bench_scaling` produces scaling curves
- [ ] `bench_backpressure` measures drop rate and recovery
- [ ] `bench_scheduler` measures timer accuracy
- [ ] `bench_mailbox_comparison` compares Mutex vs MPSC
- [ ] All benchmarks accept `--mailbox` flag
- [ ] All benchmarks support `--json` output

### 11.2 CLI Integration

- [ ] `v2 benchmark list` shows all 7 benchmarks
- [ ] `v2 benchmark latency --help` shows usage
- [ ] All benchmarks run via `v2 benchmark <name> [args]`
- [ ] Results are formatted correctly

### 11.3 Code Quality

- [ ] All benchmarks follow existing code style
- [ ] No compiler warnings
- [ ] No memory leaks (ASan clean)
- [ ] No data races (TSan clean)

### 11.4 Documentation

- [ ] Each benchmark has clear description
- [ ] Usage examples in help text
- [ ] Expected output documented

---

## Appendix: Benchmark Implementation Patterns

### A.1 Common Pattern: Creating ActorSystem

```cpp
// All benchmarks follow this pattern
Metrics::setEnabled(false);  // Disable metrics for clean measurement
ActorSystem actorSystem(workers, maxBatch);  // or with MailboxType
// ... create actors ...
actorSystem.start();
auto startTime = Time::now();
// ... run benchmark ...
auto endTime = Time::now();
actorSystem.stop();
Metrics::setEnabled(wasEnabled);
```

### A.2 Common Pattern: Waiting for Completion

```cpp
// Option 1: Spin-wait with atomic counter
while(counter.load(std::memory_order_relaxed) < target) {
    Sleep::sleepMs(10);  // or yield()
}

// Option 2: Condition variable (for latency benchmark)
std::mutex mtx;
std::condition_variable cv;
bool done = false;
// ... in handler: { done = true; cv.notify_one(); }
{
    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::milliseconds(1000), [&]{ return done; });
}
```

### A.3 Common Pattern: Statistics Calculation

```cpp
template<typename T>
struct Stats {
    double mean;
    double median;
    double p95;
    double p99;
    double min;
    double max;
    double stddev;
};

template<typename T>
Stats<T> calculateStats(std::vector<T>& data) {
    std::sort(data.begin(), data.end());
    size_t n = data.size();

    Stats<T> stats;
    stats.mean = std::accumulate(data.begin(), data.end(), 0.0) / n;
    stats.median = data[n / 2];
    stats.p95 = data[n * 95 / 100];
    stats.p99 = data[n * 99 / 100];
    stats.min = data.front();
    stats.max = data.back();

    // Standard deviation
    double sumSquaredDiff = 0;
    for(const auto& val : data) {
        sumSquaredDiff += (val - stats.mean) * (val - stats.mean);
    }
    stats.stddev = std::sqrt(sumSquaredDiff / n);

    return stats;
}
```

### A.4 Common Pattern: Thread Management

```cpp
// Producer threads
std::vector<std::thread> producers;
for(int i = 0; i < numProducers; i++) {
    producers.emplace_back([&, i] {
        for(int j = 0; j < msgsPerProducer; j++) {
            // ... send message ...
        }
    });
}

// Wait for all producers
for(auto& t : producers) {
    t.join();
}

// Consumer (single thread, guaranteed by actor system)
// ... handled by worker threads internally ...
```
