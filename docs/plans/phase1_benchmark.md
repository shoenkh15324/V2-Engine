# 2단계: 벤치마크 프레임워크 완성

> **기간**: 4-8주차 (3-4주, 1단계와 겹침)
> **목표**: 빈 벤치마크 스캐폴드 모두 완성 + 메일박스 비교 벤치마크 추가
> **전제 조건**: 1단계 (IMailbox 인터페이스와 최소한 MutexMailbox가 작동)

---

## 목차

1. [목표](#1-목표)
2. [현재 벤치마크 프레임워크](#2-현재-벤치마크-프레임워크)
3. [벤치마크 설계: 지연 시간](#3-벤치마크-설계-지연-시간)
4. [벤치마크 설계: 경쟁](#4-벤치마크-설계-경쟁)
5. [벤치마크 설계: 스케일링](#5-벤치마크-설계-스케일링)
6. [벤치마크 설계: 백프레셔](#6-벤치마크-설계-백프레셔)
7. [벤치마크 설계: 스케줄러](#7-벤치마크-설계-스케줄러)
8. [벤치마크 설계: 메일박스 비교](#8-벤치마크-설계-메일박스-비교)
9. [CLI 통합](#9-cli-통합)
10. [구현 단계](#10-구현-단계)
11. [검증 체크리스트](#11-검증-체크리스트)

---

## 1. 목표

### 1.1 주요 목표

1. 5개 빈 벤치마크 스캐폴드 구현 (지연 시간, 경쟁, 스케일링, 백프레셔, 스케줄러)
2. 메일박스 비교 벤치마크 구현 (뮤텍스 vs MPSC)
3. 벤치마크 결과에 JSON 출력 형식 추가
4. 나란한 실행을 위한 `--compare` 플래그 추가
5. 모든 벤치마크가 기존 CLI (`v2 benchmark`)와 통합되도록 보장

### 1.2 벤치마크 커버리지 매트릭스

| 벤치마크 | 측정 대상 | 주요 지표 | 설정 |
|-----------|------------------|-------------|----------------|
| **throughput** | 초당 메시지 수 | 메시지/초, ns/메시지 | 워커, 액터, maxbatch, 메일박스 |
| **latency** | 메시지당 지연 시간 | 평균, p50, p95, p99, p999 | 워커, 액터, 메일박스 |
| **contention** | 경쟁 하에서의 성능 | 메시지/초 vs 프로듀서 | 프로듀서 수, 메일박스 |
| **scaling** | 스케일링 효율성 | 메시지/초 vs 워커/액터 | 워커 범위, 액터 범위 |
| **backpressure** | 오버플로우 동작 | 드롭율, 회복 시간 | 메일박스 크기, 채우기 속도 |
| **scheduler** | 타이머 정밀도 | 타이머 정확도, 오버헤드 | 타이머 수, 간격 |
| **comparison** | 메일박스 변형 비교 | 모든 지표 | 메일박스 타입, 설정 |

---

## 2. 현재 벤치마크 프레임워크

### 2.1 기존 인프라

**파일**: `src/core/perf/benchmark/i_benchmark.hpp`

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
    Config config;  // 워커, 액터, maxBatch, 메일박스 크기
    int iterations;
    uint64_t totalDurationNs;
    double throughputPerSec;
    double avgLatencyNs;
    std::vector<ActorSnap> actorSnaps;
};
```

**파일**: `src/core/perf/benchmark/benchmark.cpp`

- `Benchmark::registerBenchmark(name, creator)` - 자체 등록
- `Benchmark::list()` - 등록된 모든 벤치마크 이름 반환
- `Benchmark::run(name, args)` - 인스턴스화 및 벤치마크 실행
- `Benchmark::runAll()` - 요약과 함께 모든 벤치마크 실행

### 2.2 기존 처리량 벤치마크

**파일**: `src/core/perf/benchmark/bench_throughput.cpp`

```cpp
IBenchmark::Result ThroughputBenchmark::run(const Args& args) {
    // 1. 인자 파싱 (워커, 액터, 반복, maxbatch)
    // 2. 워커와 maxbatch로 ActorSystem 생성
    // 3. N개의 BenchActor 생성
    // 4. 시스템 시작
    // 5. 라운드로빈으로 반복 메시지 전송
    // 6. 모든 메시지 처리될 때까지 대기 (슬립과 함께 스핀-웨이트)
    // 7. 시스템 중지
    // 8. 처리량 및 지연 시간 계산
    // 9. Result 반환
}
```

### 2.3 BenchActor

**파일**: `src/core/perf/benchmark/bench_throughput.hpp`

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

**설계**: 원자적 변수만 증가하는 최소한의 액터. 핸들러 로직이 아닌 시스템 오버헤드를 측정합니다.

---

## 3. 벤치마크 설계: 지연 시간

### 3.1 목적

메시지당 종단간 지연 시간 측정: `enqueue()`부터 `handle()` 완료까지의 시간.

### 3.2 설계

**접근**: 핑퐁 패턴
1. 프로듀서가 타임스탬프와 함께 메시지 전송
2. 컨슈머 (핸들러)가 수신 타임스탬프 기록
3. 지연 시간 = 수신 - 전송 계산
4. N개 메시지에 대해 반복, 통계 보고

**과제**: 스레드 간 시계 동기화. `std::chrono::steady_clock` (단조로움, 스레드 안전) 사용.

### 3.3 구현 계획

**파일**: `src/core/perf/benchmark/bench_latency.cpp`

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
    // 1. 인자 파싱
    // 2. ActorSystem 생성
    // 3. 단일 LatencyBenchActor 생성
    // 4. 시스템 시작
    // 5. 각 반복마다:
    //    a. 전송 타임스탬프 기록
    //    b. 메시지 전송
    //    c. 핸들러가 처리할 때까지 대기 (원자적 변수로 스핀-웨이트)
    // 6. 시스템 중지
    // 7. 통계 계산 (평균, p50, p95, p99, p999)
    // 8. Result 반환
}
```

### 3.4 Result 확장

지연 시간 분포를 포함하도록 `IBenchmark::Result` 확장:

```cpp
struct Result {
    // ... 기존 필드 ...

    // 지연 시간 분포 (새)
    double p50LatencyNs;
    double p95LatencyNs;
    double p99LatencyNs;
    double p999LatencyNs;
    double minLatencyNs;
    double maxLatencyNs;
    std::vector<double> latencyHistogram;  // 선택 사항
};
```

### 3.5 통계 계산

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

### 3.6 CLI 사용법

```
v2 benchmark latency --actors 1 --workers 4 --iterations 100000 --mailbox mutex
v2 benchmark latency --actors 1 --workers 4 --iterations 100000 --mailbox mpsc
```

### 3.7 예상 출력

```
=== 벤치마크: latency ===
메시지당 종단간 지연 시간 측정

[테스트 설정]
  워커:      4
  액터:      1
  메일박스:   mpsc
  반복:      100000

[지연 시간 통계]
  평균:     1234.56 ns
  P50:      1100.00 ns
  P95:      1800.00 ns
  P99:      2500.00 ns
  P999:     5000.00 ns
  최소:     800.00 ns
  최대:     15000.00 ns

  총 시간: 1234.56 ms
```

---

## 4. 벤치마크 설계: 경쟁

### 4.1 목적

여러 프로듀서가 동일한 액터를 대상으로 할 때 높은 경쟁 하에서의 성능 저하 측정.

### 4.2 설계

**접근**: 고정 컨슈머, 가변 프로듀서
1. 1개 액터 생성
2. N개 프로듀서 스레드 생성
3. 각 프로듀서가 M개 메시지 전송
4. 총 처리량 측정
5. N을 1에서 64까지 변경

### 4.3 구현 계획

**파일**: `src/core/perf/benchmark/bench_contention.cpp`

```cpp
IBenchmark::Result ContentionBenchmark::run(const Args& args) {
    // 1. 인자 파싱 (프로듀서 수, 프로듀서당 메시지, 메일박스)
    // 2. ActorSystem 생성
    // 3. 단일 BenchActor 생성
    // 4. 시스템 시작
    // 5. 프로듀서 수개 스레드 생성
    // 6. 각 스레드가 프로듀서당 메시지 수만큼 전송
    // 7. 모든 스레드가 끝날 때까지 대기
    // 8. 모든 메시지 처리될 때까지 대기
    // 9. 시스템 중지
    // 10. 프로듀서 수별 처리량 계산
    // 11. Result 반환
}
```

### 4.4 Result 확장

```cpp
struct Result {
    // ... 기존 필드 ...

    // 경쟁 분석 (새)
    std::vector<ContentionDataPoint> contentionCurve;
};

struct ContentionDataPoint {
    int numProducers;
    double throughputPerSec;
    double avgLatencyNs;
    double contentionRatio;  // 처리량 / 단일 프로듀서 처리량
};
```

### 4.5 예상 출력

```
=== 벤치마크: contention ===
다중 프로듀서 경쟁 하에서의 성능 측정

[테스트 설정]
  액터:      1
  MaxBatch:  32
  메일박스:   mpsc
  프로듀서당 메시지: 10000

[경쟁 곡선]
프로듀서    처리량       지연 시간     비율
------------------------------------------------
        1    50000.00     20.00 ns    1.00x
        2    45000.00     22.22 ns    0.90x
        4    40000.00     25.00 ns    0.80x
        8    35000.00     28.57 ns    0.70x
       16    30000.00     33.33 ns    0.60x
       32    25000.00     40.00 ns    0.50x
       64    20000.00     50.00 ns    0.40x
```

### 4.6 포트폴리오를 위한 핵심 통찰

이 벤치마크는 다양한 메일박스 구현의 **스케일링 한계**를 보여줍니다:
- MutexMailbox: 프로듀서 수에 비례하여 처리량 저하 (뮤�텍스 경쟁)
- MPSCMailbox: 캐시 라인 바운싱이 시작될 때까지 처리량 유지

---

## 5. 벤치마크 설계: 스케일링

### 5.1 목적

워커와 액터 수에 따라 처리량이 어떻게 스케일링되는지 측정.

### 5.2 설계

**2차원 스케일링**:
1. **워커 스케일링**: 액터 고정, 워커 변경 (1, 2, 4, 8, 16, 32, 64)
2. **액터 스케일링**: 워커 고정, 액터 변경 (1, 2, 4, 8, 16, 32, 64)

### 5.3 구현 계획

**파일**: `src/core/perf/benchmark/bench_scaling.cpp`

```cpp
IBenchmark::Result ScalingBenchmark::run(const Args& args) {
    // 1. 인자 파싱 (반복, maxbatch, 메일박스)
    // 2. 워커 스케일링:
    //    각 워커_수 in [1, 2, 4, 8, 16, 32, 64]에 대해:
    //      워커_수개 워커로 ActorSystem 생성
    //      액터 생성 (16으로 고정)
    //      처리량 벤치마크 실행
    //      결과 기록
    // 3. 액터 스케일링:
    //    각 액터_수 in [1, 2, 4, 8, 16, 32, 64]에 대해:
    //      고정된 16개 워커로 ActorSystem 생성
    //      액터_수개 액터 생성
    //      처리량 벤치마크 실행
    //      결과 기록
    // 4. 결합된 결과 반환
}
```

### 5.4 Result 확장

```cpp
struct Result {
    // ... 기존 필드 ...

    // 스케일링 분석 (새)
    std::vector<ScalingDataPoint> workerScaling;
    std::vector<ScalingDataPoint> actorScaling;
};

struct ScalingDataPoint {
    int count;  // 워커 또는 액터 수
    double throughputPerSec;
    double efficiency;  // 처리량 / (수 * 단일_유닛_처리량)
};
```

### 5.5 예상 출력

```
=== 벤치마크: scaling ===
워커와 액터에 따른 처리량 스케일링 측정

[테스트 설정]
  반복:      100000
  MaxBatch:  32
  메일박스:   mpsc

[워커 스케일링] (액터=16 고정)
워커     처리량       효율성
----------------------------------------
      1     8000.00       1.00x
      2    15000.00       0.94x
      4    28000.00       0.88x
      8    48000.00       0.75x
     16    72000.00       0.56x
     32    85000.00       0.33x
     64    90000.00       0.18x

[액터 스케일링] (워커=16 고정)
액터     처리량       효율성
----------------------------------------
      1     5000.00       1.00x
      2     9800.00       0.98x
      4    19000.00       0.95x
      8    36000.00       0.90x
     16    68000.00       0.85x
     32    95000.00       0.74x
     64    110000.00      0.54x
```

### 5.6 포트폴리오를 위한 핵심 통찰

액터 시스템에서의 **아멀달의 법칙**을 시연:
- 워커 스케일링은 공유 디스패처의 경쟁으로 인해 평탄해짐
- 액터 스케일링은 액터당 경쟁을 줄여 처리량 향상
- 최적점은 메일박스 구현과 하드웨어에 따라 다름

---

## 6. 벤치마크 설계: 백프레셔

### 6.1 목적

메일박스가 가득 찼을 때의 동작 측정: 드롭율, 회복 시간, 시스템 안정성.

### 6.2 설계

**접근**: 범람 후 회복
1. 작은 메일박스를 가진 액터 생성 (예: 용량=64)
2. 컨슈머가 처리할 수 있는 것보다 빠르게 메시지 전송
3. 드롭율 측정
4. 범람 중지
5. 회복 시간 측정 (메일박스가 비울 때까지의 시간)

### 6.3 구현 계획

**파일**: `src/core/perf/benchmark/bench_backpressure.cpp`

```cpp
IBenchmark::Result BackpressureBenchmark::run(const Args& args) {
    // 1. 인자 파싱 (메일박스 크기, 범람 속도, 범람 지속 시간 ms)
    // 2. ActorSystem 생성
    // 3. 작은 메일박스를 가진 단일 BenchActor 생성
    // 4. 시스템 시작
    // 5. 범람 단계:
    //    - 범람 지속 시간 동안 범람 속도로 메시지 전송
    //    - 드롭 수 카운트 (push가 false 반환)
    // 6. 회복 단계:
    //    - 전송 중지
    //    - 메일박스가 비워질 때까지 시간 측정
    // 7. 시스템 중지
    // 8. 드롭율 및 회복 시간 계산
    // 9. Result 반환
}
```

### 6.4 Result 확장

```cpp
struct Result {
    // ... 기존 필드 ...

    // 백프레셔 분석 (새)
    size_t totalSent;
    size_t totalDropped;
    double dropRate;
    uint64_t recoveryTimeNs;
    size_t peakMailboxDepth;
};
```

### 6.5 예상 출력

```
=== 벤치마크: backpressure ===
메일박스 오버플로우 동작 측정

[테스트 설정]
  메일박스 크기:    64
  범람 속도:        1000000 메시지/초
  범람 지속 시간:   100 ms

[결과]
  총 전송:          100000
  총 드롭:          85000
  드롭율:           85.00%
  회복 시간:        45.23 ms
  최대 깊이:        64

[분석]
  범람 동안 메일박스가 85%의 메시지를 드롭했습니다.
  회복에 45.23ms가 걸렸습니다.
  메일박스 크기를 늘리거나 프로듀서 속도를 줄이는 것을 고려하십시오.
```

### 6.6 포트폴리오를 위한 핵심 통찰

액터 시스템에서의 **흐름 제어**를 시연:
- 메일박스 크기 조정의 중요성을 보여줌
- 다양한 메일박스 변형이 오버플로우를 처리하는 방법을 비교
- 용량 계획을 위한 데이터 제공

---

## 7. 벤치마크 설계: 스케줄러

### 7.1 목적

타이머 스케줄링 정밀도 및 오버헤드 측정.

### 7.2 설계

**접근**: 타이머 정확도 테스트
1. 알려진 간격으로 N개 타이머 생성
2. 실제 발사 시간 측정
3. 정확도 계산 (실제 - 예상)
4. 오버헤드 측정 (타이머 생성 + 취소 비용)

### 7.3 구현 계획

**파일**: `src/core/perf/benchmark/bench_scheduler.cpp`

```cpp
IBenchmark::Result SchedulerBenchmark::run(const Args& args) {
    // 1. 인자 파싱 (타이머 수, 간격 ms, 지속 시간 ms)
    // 2. ActorSystem 생성
    // 3. 발사 시간을 기록하는 TimerBenchActor 생성
    // 4. 시스템 시작
    // 5. 간격 ms로 타이머 수개 타이머 생성
    // 6. 지속 시간 ms 동안 실행
    // 7. 발사 시간 수집
    // 8. 시스템 중지
    // 9. 정확도 통계 계산
    // 10. Result 반환
}
```

### 7.4 Result 확장

```cpp
struct Result {
    // ... 기존 필드 ...

    // 스케줄러 분석 (새)
    double timerAccuracyMeanNs;
    double timerAccuracyStdDevNs;
    uint64_t timerOverheadNs;  // 하나의 타이머 생성 + 취소 시간
    size_t totalFirings;
};
```

### 7.5 예상 출력

```
=== 벤치마크: scheduler ===
타이머 스케줄링 정밀도 측정

[테스트 설정]
  타이머:    16
  간격:      100 ms
  지속 시간: 5000 ms

[결과]
  총 발사:         800
  정확도 평균:     125.34 us
  정확도 표준편차:  45.67 us
  타이머 오버헤드:  2.45 us

[분석]
  타이머 정확도는 Linux timerfd의 예상 범위 내입니다.
  오버헤드는 epoll_wait 깨우기 지연 시간에 지배됩니다.
```

---

## 8. 벤치마크 설계: 메일박스 비교

### 8.1 목적

동일한 조건에서 MutexMailbox vs MPSCMailbox를 직접 비교.

### 8.2 설계

**접근**: 다른 메일박스 타입으로 동일한 벤치마크 실행, 나란한 결과 보고

### 8.3 구현 계획

**파일**: `src/core/perf/benchmark/bench_mailbox_comparison.cpp`

```cpp
IBenchmark::Result MailboxComparisonBenchmark::run(const Args& args) {
    // 1. 인자 파싱
    // 2. 각 MailboxType in [Mutex, MPSC]에 대해:
    //    a. 메일박스 타입으로 ActorSystem 생성
    //    b. 액터 생성
    //    c. 처리량 벤치마크 실행
    //    d. 결과 기록
    //    e. 지연 시간 벤치마크 실행 (선택 사항)
    //    f. 결과 기록
    // 3. 속도 비율 계산
    // 4. 결합된 결과 반환
}
```

### 8.4 Result 구조

```cpp
struct Result {
    // ... 기존 필드 ...

    // 비교 결과 (새)
    std::vector<MailboxResult> variants;
};

struct MailboxResult {
    MailboxType type;
    double throughputPerSec;
    double avgLatencyNs;
    double p99LatencyNs;
};
```

### 8.5 예상 출력

```
=== 벤치마크: comparison ===
메일박스 구현 비교

[테스트 설정]
  워커:      16
  액터:      16
  MaxBatch:  32
  반복:      100000

[결과]
메일박스   처리량       평균 지연시간  P99 지연시간  속도비
----------------------------------------------------------------
  뮤텍스    4874.27      205159.01 ns  450000.00 ns  1.00x
  MPSC    12500.00       80000.00 ns  150000.00 ns  2.56x

[분석]
  MPSCMailbox가 2.56배 처리량 개선을 보여줍니다.
  P99 지연 시간이 66.67% 감소했습니다.
  잠금 해제 설계가 뮤텍스 경쟁을 제거합니다.
```

### 8.6 매개변수 설정

비교 벤치마크는 여러 설정을 지원해야 합니다:

```
v2 benchmark comparison --workers 4 --actors 4    # 낮은 경쟁
v2 benchmark comparison --workers 16 --actors 16  # 균형
v2 benchmark comparison --workers 64 --actors 16  # 높은 경쟁
v2 benchmark comparison --workers 16 --actors 64  # 많은 액터
```

---

## 9. CLI 통합

### 9.1 확장된 CLI 명령

**파일**: `src/app/cli/cli_app.cpp`

```cpp
// subCmds_ 목록에 추가:
{"latency", "[--actors N] [--workers N] [--iterations N] [--mailbox type]", "메시지당 지연 시간 측정"},
{"contention", "[--producers N] [--msgs N] [--mailbox type]", "경쟁 성능 측정"},
{"scaling", "[--iterations N] [--mailbox type]", "스케일링 효율성 측정"},
{"backpressure", "[--mailbox-size N] [--flood-rate N] [--mailbox type]", "오버플로우 동작 측정"},
{"scheduler", "[--timers N] [--interval N] [--duration N]", "타이머 정밀도 측정"},
{"comparison", "[--workers N] [--actors N] [--iterations N]", "메일박스 구현 비교"},
```

### 9.2 공통 인자

모든 벤치마크는 다음 공통 인자를 허용합니다:

| 인자 | 설명 | 기본값 |
|----------|-------------|---------|
| `--workers N` | 워커 스레드 수 | 4 |
| `--actors N` | 액터 수 | 16 |
| `--iterations N` | 메시지 수 | 10000 |
| `--maxbatch N` | 최대 배치 크기 | 32 |
| `--mailbox type` | 메일박스 타입 (mutex/mpsc) | mutex |

### 9.3 JSON 출력

모든 벤치마크에 `--json` 플래그 추가:

```
v2 benchmark throughput --actors 16 --workers 64 --json
```

출력:
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

### 9.4 구현

`IBenchmark::Result`에 JSON 직렬화 확장:

```cpp
// i_benchmark.hpp에서
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

## 10. 구현 단계

### 4-5주차: 코어 벤치마크

| 날 | 작업 | 산출물 |
|-----|------|-------------|
| 1-2 | `bench_latency.cpp` 구현 | 지연 시간 벤치마크 |
| 3-4 | `bench_contention.cpp` 구현 | 경쟁 벤치마크 |
| 5-6 | `bench_scaling.cpp` 구현 | 스케일링 벤치마크 |
| 7 | 세 가지 테스트 및 디버깅 | 작동하는 벤치마크 |

### 5-6주차: 나머지 벤치마크

| 날 | 작업 | 산출물 |
|-----|------|-------------|
| 8-9 | `bench_backpressure.cpp` 구현 | 백프레셔 벤치마크 |
| 10-11 | `bench_scheduler.cpp` 구현 | 스케줄러 벤치마크 |
| 12-13 | `bench_mailbox_comparison.cpp` 구현 | 비교 벤치마크 |
| 14 | 세 가지 테스트 및 디버깅 | 작동하는 벤치마크 |

### 6-7주차: CLI 및 출력

| 날 | 작업 | 산출물 |
|-----|------|-------------|
| 15-16 | 모든 벤치마크에 `--mailbox` 플래그 추가 | 업데이트된 CLI |
| 17-18 | `--json` 출력 형식 추가 | JSON 출력 |
| 19-20 | 새로운 필드로 `IBenchmark::Result` 업데이트 | 확장된 결과 |
| 21 | CLI 통합 테스트 | 작동하는 CLI |

### 7-8주차: 마무리

| 날 | 작업 | 산출물 |
|-----|------|-------------|
| 22-23 | 벤치마크 설명 및 도움말 텍스트 추가 | 사용자 친화적 출력 |
| 24-25 | 성능 최적화 | 최적화된 벤치마크 |
| 26-27 | 문서 업데이트 | 업데이트된 문서 |
| 28 | 최종 테스트 | 모든 벤치마크 작동 |

---

## 11. 검증 체크리스트

### 11.1 기능

- [ ] `bench_latency`가 정확한 지연 시간 통계 생성
- [ ] `bench_contention`이 경쟁 곡선 생성
- [ ] `bench_scaling`이 스케일링 곡선 생성
- [ ] `bench_backpressure`가 드롭율 및 회복 측정
- [ ] `bench_scheduler`가 타이머 정확도 측정
- [ ] `bench_mailbox_comparison`이 뮤텍스 vs MPSC 비교
- [ ] 모든 벤치마크가 `--mailbox` 플래그를 받음
- [ ] 모든 벤치마크가 `--json` 출력 지원

### 11.2 CLI 통합

- [ ] `v2 benchmark list`가 7개 벤치마크를 보여줌
- [ ] `v2 benchmark latency --help`가 사용법을 보여줌
- [ ] 모든 벤치마크가 `v2 benchmark <name> [args]`로 실행 가능
- [ ] 결과가 올바르게 포맷됨

### 11.3 코드 품질

- [ ] 모든 벤치마크가 기존 코드 스타일 준수
- [ ] 컴파일러 경고 없음
- [ ] 메모리 누수 없음 (ASan 깨끗)
- [ ] 데이터 레이스 없음 (TSan 깨끗)

### 11.4 문서

- [ ] 각 벤치마크에 명확한 설명
- [ ] 도움말 텍스트에 사용 예제
- [ ] 예상 출력 문서화

---

## 부록: 벤치마크 구현 패턴

### A.1 공통 패턴: ActorSystem 생성

```cpp
// 모든 벤치마크가 이 패턴을 따름
Metrics::setEnabled(false);  // 깨끗한 측정을 위해 메트릭 비활성화
ActorSystem actorSystem(workers, maxBatch);  // 또는 MailboxType과 함께
// ... 액터 생성 ...
actorSystem.start();
auto startTime = Time::now();
// ... 벤치마크 실행 ...
auto endTime = Time::now();
actorSystem.stop();
Metrics::setEnabled(wasEnabled);
```

### A.2 공통 패턴: 완료 대기

```cpp
// 옵션 1: 원자적 카운터로 스핀-웨이트
while(counter.load(std::memory_order_relaxed) < target) {
    Sleep::sleepMs(10);  // 또는 yield()
}

// 옵션 2: 조건 변수 (지연 시간 벤치마크용)
std::mutex mtx;
std::condition_variable cv;
bool done = false;
// ... 핸들러에서: { done = true; cv.notify_one(); }
{
    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::milliseconds(1000), [&]{ return done; });
}
```

### A.3 공통 패턴: 통계 계산

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

    // 표준편차
    double sumSquaredDiff = 0;
    for(const auto& val : data) {
        sumSquaredDiff += (val - stats.mean) * (val - stats.mean);
    }
    stats.stddev = std::sqrt(sumSquaredDiff / n);

    return stats;
}
```

### A.4 공통 패턴: 스레드 관리

```cpp
// 프로듀서 스레드
std::vector<std::thread> producers;
for(int i = 0; i < numProducers; i++) {
    producers.emplace_back([&, i] {
        for(int j = 0; j < msgsPerProducer; j++) {
            // ... 메시지 전송 ...
        }
    });
}

// 모든 프로듀서 대기
for(auto& t : producers) {
    t.join();
}

// 컨슈머 (단일 스레드, 액터 시스템으로 보장)
// ... 내부적으로 워커 스레드에 의해 처리됨 ...
```
