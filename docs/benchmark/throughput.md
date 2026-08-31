# 쓰루풋 벤치마크

## 목적

> **연구 질문:** "액터 시스템이 초당 최대 몇 개의 메시지를 처리할 수 있는가?"

push부터 handle까지의 **최대 메시지 처리율**을 측정합니다. 이는 **시스템 성능의 상한선**을 결정합니다.

## 설계 원칙

### 왜 중요한가

쓰루풋은 액터 시스템에서 가장 중요한 지표입니다. 파이프라인을 통해 흐를 수 있는 메시지 수를 결정하며, 이는 애플리케이션 용량에 직접적인 영향을 미칩니다.

### 측정 방법

```
1. Setup:    W workers, A actors, maxBatch=M, ActorSystem 생성
2. Inject:   메인 스레드가 N개 메시지 push (round-robin)
3. Wait:     processed == N until (30초 타임아웃)
4. Analyze:  throughput = N / elapsed [msgs/sec]
```

핸들러는 `atomic counter`만 증가시킵니다 — 실제 계산은 없습니다. 이를 통해 **동기화 오버헤드**만 격리하여 측정합니다.

### 파라미터 참조

| 파라미터 | 기본값 | 설명 |
|---------|--------|------|
| `--workers` | 4 | 워커 스레드 수 |
| `--actors` | 1 | 대상 액터 수 |
| `--iterations` | 100,000 | 메시지 수 |
| `--maxbatch` | 32 | 워커당 최대 배치 크기 |
| `--warmup` | 0 | 웜업 메시지 수 |
| `--mailbox` | 0 | 메일박스 용량 (0=자동) |

## 테스트 환경

| 항목 | 사양 |
|------|------|
| CPU | AMD Ryzen 7 9800X3D (8 Physical, 16 Logical, Zen 5) |
| 캐시 | L3 96MB (3D V-Cache), L2 1MB/core |
| RAM | 8 GB (DDR5) |
| OS | Ubuntu 22.04.5 LTS (WSL2, 커널 6.18.33) |
| 컴파일러 | g++-14 (Ubuntu 14.3.0) |
| C++ 표준 | C++20 |
| 빌드 시스템 | CMake 3.22.1 + Ninja |
| 빌드 모드 | Release (`-O3 -DNDEBUG`) + LTO |
| 메모리 할당자 | TCMalloc 기반 Slab 풀 (커스텀) |
| 도구 | `./build/v2_bench_cli throughput` |
| 측정 방식 | Busy-wait (no sleep, 30초 타임아웃) |

## 실험 결과 (2026-08-31 실측)

### 실험 1: 워커 수 (단일 프로듀서)

**조건:** actors=1, maxBatch=32, iterations=500,000

| Workers | 쓰루풋 (msgs/sec) | 비고 |
|---------|-------------------|------|
| 1 | **27,739,238** | **최적** |
| 2 | 24,551,551 | |
| 4 | 20,441,662 | |
| 8 | 25,559,762 | |
| 16 | 33,885,825 | 티어3 스핀 활성화 |

**분석:**
- **workers=1에서 피크**: 27.7M msgs/sec
- **workers=2~8**: readyQueue 뮤텍스 경쟁으로 약간 감소
- **workers=16**: 티어3 스핀(dequeue bypass)으로 인해 오히려 성능 향상
- **핵심:** LockFreeMailbox는 **단일 워커**에서 최적, 다중 워커는 티어3 스핀으로 완화

### 실험 2: 액터 수 (단일 프로듀서)

**조건:** workers=1, maxBatch=32, iterations=500,000

| Actors | 쓰루풋 (msgs/sec) | 비고 |
|--------|-------------------|------|
| 1 | **27,739,238** | 기준 |
| 2 | 28,978,684 | Ping-Pong (1:1 교환) |
| 4 | 35,128,138 | **최적** |
| 16 | 38,921,423 | |

**분석:**
- **actors=16에서 최고**: 38.9M msgs/sec
- **멀티 액터**: 독립 메일박스로 병렬 push 가능하여 쓰루풋 증가
- **핵심:** 라운드로빈 배분 시 액터 수가 많을수록 쓰루풋 향상

### 실험 3: 워커 × 액터 조합

**조건:** maxBatch=32, iterations=500,000

| Workers | Actors | 쓰루풋 (msgs/sec) | 비고 |
|---------|--------|-------------------|------|
| 1 | 1 | **27,739,238** | 기준 |
| 4 | 4 | 19,921,046 | |
| 4 | 16 | 28,485,023 | |
| 8 | 8 | 17,436,814 | |
| 8 | 16 | 27,636,646 | |
| 16 | 16 | **299,698** | **붕괴** |

**분석:**
- **16W 16A에서 붕괴**: 0.3M msgs/sec (100배 이상 감소)
- **단일 프로듀서 + 멀티 액터 시 dispatch 병목**: `enqueue()` → `dispatch()` 경로가 싱글 스레드
- **멀티 프로듀서(8P) 사용 시**: 73.3M msgs/sec 달성 가능

### 실험 4: 멀티 프로듀서 벤치마크

**조건:** actors=16, maxBatch=32, iterations=500,000

| Workers | Producers | 쓰루풋 (msgs/sec) | 비고 |
|---------|-----------|-------------------|------|
| 1 | 8 | 48,695,332 | |
| 4 | 4 | 59,378,296 | |
| 4 | 8 | **73,272,523** | **최적** |
| 8 | 8 | 66,451,695 | |

**분석:**
- **4W 16A 8P에서 최적**: 73.3M msgs/sec
- **멀티 프로듀서**: 단일 프로듀서의 dispatch 병목 우회
- **핵심:** 멀티 프로듀서는 스케일링 붕괴의 효과적인 우회 방법

## 출력 예시

```
=== throughput ===
메시지 쓰루풋 측정

[Config] workers=4 actors=1 maxBatch=32 iterations=500000
Throughput: 20,441,662 msgs/sec
Total Time: 24.46 ms
```

## 기술적 상세

### 동기화 경로

```
메인 스레드:  receiveMsg(Tick)
    └→ ActorRuntime::enqueue()
        ├→ Mailbox::push()           [CAS 기반 락프리]
        └→ Dispatcher::dispatch()
            ├→ readyQueue_.push()    [뮤텍스 1]
            └→ sema_.release()       [세마포어 신호]

워커 스레드:  sema_.acquire()      [세마포어 대기]
    └→ ActorRuntime::run(maxBatch)
        └→ Mailbox::pop()            [CAS 기반 락프리]
            └→ Actor::handle()
                └→ counter_.fetch_add(1)
```

### 확장 패턴 요약

| 변수 | 효과 | 원인 |
|------|------|------|
| workers ↑ | **약간 감소** | readyQueue 뮤텍스 경쟁 |
| actors ↑ | **증가** | 독립 메일박스 병렬 push |
| producers ↑ | **증가** | dispatch 병목 우회 |
| handlers ↓ | **증가** | 핸들러 오버헤드 감소 |

## 분석

### 핵심 발견

1. **피크 쓰루풋: 73.3M msgs/sec** (4W, 16A, 8P)
   - 멀티 프로듀서 + 멀티 액터에서 최적
   - 단일 프로듀서 대비 **2.6배**

2. **단일 워커 피크: 27.7M msgs/sec**
   - 가벼운 핸들러에서 세마포어 비용 제외
   - ReadyQueue 뮤텍스가 유일한 병목

3. **16W 16A에서 스케일링 붕괴**
   - 299K msgs/sec로 100배 성능 하락
   - **원인**: 단일 프로듀서의 dispatch 경로가 싱글 스레드

4. **멀티 프로듀서가 붕괴 우회**
   - 4W 16A 8P: 73.3M msgs/sec
   - enqueue()를 여러 스레드에서 병렬 실행

### 상용 엔진 비교

| 엔진 | 쓰루풋 | V2 대비 |
|------|--------|---------|
| **V2-Engine** (4W 16A 8P) | **73.3M msgs/sec** | 기준 |
| **V2-Engine** (1W 1A 1P) | **27.7M msgs/sec** | 기준 |
| CAF (in-process) | ~9.4M msgs/sec | V2가 **7.8배 빠름** |
| CAF (최적화 분기) | ~35M msgs/sec | V2가 **2.1배 빠름** |
| SObjectizer (direct ch) | ~17M msgs/sec | V2가 **4.3배 빠름** |
| Actix (Rust, 100A) | ~4.7M msgs/sec | V2가 **15.6배 빠름** |
| Ractor (Rust, 100A) | ~3.1M msgs/sec | V2가 **23.6배 빠름** |

### 권장사항

| 우선순위 | 권장사항 | 예상 효과 |
|---------|---------|----------|
| 최우선 | 멀티 프로듀서 사용 (4P~8P) | 73M+ msgs/sec |
| 높음 | 4~8 액터로 라운드로빈 배분 | 35~39M msgs/sec |
| 중간 | workers=1~2 (단일 프로듀서 시) | 27~29M msgs/sec |
| 낮음 | 티어3 스핀 튜닝 | workers=16 성능 향상 |

## 결론

### 요약

시스템은 4W 16A 8P에서 **73.3M msgs/sec 피크 쓰루풋**을 달성합니다. 단일 프로듀서에서는 16 액터 + 16 워커 시 스케일링 붕괴가 발생하지만, 멀티 프로듀서를 사용하면 우회 가능합니다. V2-Engine은 C++ 범용 액터 프레임워크(CAF, SObjectizer) 대비 **2-8배**, Rust 액터 프레임워크(Actix, Ractor) 대비 **15-24배** 빠릅니다.

### 사용 시점

| 시나리오 | 권장사항 |
|---------|---------|
| 최대 쓰루풋 | workers=4, actors=16, producers=8 |
| 단일 워커 최적 | workers=1, actors=1~4 |
| 멀티 워커 확장 | 멀티 프로듀서 반드시 사용 |

## 참고 문헌

- 소스: `bench/bench_throughput.cpp`
- 실행: `./build/v2_bench_cli throughput [options]`
- 아키텍처: [메일박스 선택](../architecture/mailbox_comparison.md)
