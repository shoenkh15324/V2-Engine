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
| CPU | AMD Ryzen 7 9800X3D (8코어) |
| RAM | 8 GB (WSL) |
| OS | Linux (WSL Ubuntu-22.04) |
| 컴파일러 | GCC, C++20, CMake + Ninja |
| 빌드 모드 | Release (LTO 활성화) |
| 도구 | `./build/v2_cli benchmark throughput` |
| 측정 방식 | Busy-wait (no sleep, 30초 타임아웃) |

## 실험 결과

### 기본 파라미터

| 파라미터 | 값 | 설명 |
|---------|-----|------|
| iterations | 100,000 | 전체 메시지 수 |
| warmup | 1,000 | 웜업 메시지 수 |

### 실험 1: 워커 수

**조건:** actors=1, maxBatch=32, iterations=100,000

| Workers | 쓰루풋 (msgs/sec) | 비고 |
|---------|-------------------|------|
| 1 | 5,522,383 | |
| 2 | **7,109,219** | 피크 |
| 4 | 107,339 | MPSC 제약 |
| 8 | 10,128 | MPSC 제약 |

**분석:**
- **workers=2에서 피크**: 7.1M msgs/sec
- **workers=4+에서 성능 저하**: MPSC 단일 컨슈머 제약 + readyQueue 병목
- **핵심:** **소수 워커 + 가벼운 핸들러**에서 최적

### 실험 2: 액터 수

**조건:** workers=4, maxBatch=32, iterations=100,000

| Actors | 쓰루풋 (msgs/sec) | 비고 |
|--------|-------------------|------|
| 1 | 107,339 | |
| 2 | **2,223,665** | 최적 |
| 4 | 24,769 | |
| 8 | 25,039 | |

**분석:**
- **actors=2**: 2.2M msgs/sec (2개 독립 메일박스로 병렬 push 가능)
- **actors=1,4,8**: readyQueue 뮤텍스 + MPSC 제약로 낮은 성능

### 실험 3: maxBatch

**조건:** workers=4, actors=1, iterations=100,000

| maxBatch | 쓰루풋 (msgs/sec) | 비고 |
|----------|-------------------|------|
| 1 | 1,033,722 | |
| 8 | **6,202,553** | |
| 16 | **6,727,312** | 피크 |
| 32 | 107,339 | 급격한 감소 |
| 64 | **6,307,903** | |

**분석:**
- **maxBatch=16에서 피크**: 6.7M msgs/sec
- **maxBatch=32에서 급격한 감소**: readyQueue 뮤텍스 병목 최대화
- **최적 범위**: maxBatch=8~16 또는 64

### 실험 4: 메일박스 크기

**조건:** workers=4, actors=1, maxBatch=32, iterations=100,000

| 메일박스 | 쓰루풋 (msgs/sec) | 전체 시간 | 비고 |
|---------|-------------------|----------|------|
| 256 | 3,311 | 30,205 ms | 프로듀서 차단 |
| 512 | 3,309 | 30,219 ms | 프로듀서 차단 |
| 1,024 | 3,316 | 30,153 ms | 프로듀서 차단 |
| 2,048 | 3,307 | 30,240 ms | 프로듀서 차단 |
| 4,096 | 3,307 | 30,241 ms | 프로듀서 차단 |
| 8,192 | 372,109 | 269 ms | |
| 16,384 | 397,665 | 251 ms | |
| 32,768 | 344,103 | 291 ms | |
| 65,536 | 421,508 | 237 ms | |

**분석:**
- **메일박스 < 8192**: ~3,300 msgs/sec (프로듀서가 컨슈머 대기)
- **메일박스 >= 8192**: 370K+ msgs/sec (차단 없이 push)
- **핵심:** 메일박스 크기가 쓰루풋에 **가장 큰 영향**

## 출력 예시

```
=== throughput ===
메시지 쓰루풋 측정

[Test Config]
  Workers:    4
  Actors:     1
  MaxBatch:   32
  Mailbox:    100256
  Iterations: 100000
  Warmup:     1000

  Throughput: 508361.91 msgs/sec
  Total Time: 196.71 ms
```

## 기술적 상세

### 동기화 경로

```
메인 스레드:  receiveMsg(Tick)
    └→ ActorContext::enqueue()
        ├→ Mailbox::push()           [뮤텍스 1]
        └→ Dispatcher::dispatch()
            ├→ readyQueue_.push()    [뮤텍스 2]
            └→ sema_.release()       [세마포어 신호]

워커 스레드:  sema_.acquire()      [세마포어 대기]
    └→ ActorContext::run(maxBatch)
        └→ Mailbox::pop()            [뮤텍스 3]
            └→ Actor::handle()
                └→ counter_.fetch_add(1)
```

### 확장 요약

| 변수 | 효과 |
|------|------|
| workers ↑ | **감소** (MPSC + readyQueue 경쟁) |
| actors ↑ | **변동** (actors=2 피크, 나머지 낮음) |
| maxBatch ↑ | **변동** (16에서 피크, 32에서 급감) |
| mailbox ↑ | **증가** (프로듀서 차단 감소) |

## 분석

### 핵심 발견

1. **피크 쓰루풋: workers=2에서 7.1M msgs/sec**
   - 단일/이중 워커 구성에서 최적

2. **workers=4+에서 MPSC 제약**
   - 단일 컨슈머 병목이 쓰루풋 제한
   - **소수 워커 환경에 적합**

3. **maxBatch가 성능에 큰 영향**
   - maxBatch=16에서 피크 (6.7M msgs/sec)
   - maxBatch=32에서 급감
   - **maxBatch=8~16 또는 64에서 최적**

4. **메일박스 크기가 가장 큰 요인**
   - 메일박스 < 8192: ~3,300 msgs/sec (프로듀서 차단)
   - 메일박스 >= 65,536: 420K+ msgs/sec (비차단)
   - **최소 `iterations/actors + 256` 권장**

### 권장사항

| 우선순위 | 권장사항 | 예상 효과 |
|---------|---------|----------|
| 높음 | workers=1~2로 피크 쓰루풋 | 5.5~7.1M msgs/sec |
| 높음 | maxBatch=8~16 설정 | 최적 쓰루풋 |
| 중간 | 메일박스 >= 8192 설정 | 프로듀서 차단 방지 |
| 낮음 | 무거운 핸들러의 경우 워커를 CPU 코어에 맞춤 | 병렬 활용 |

## 결론

### 요약

시스템은 workers=2에서 **7.1M msgs/sec 피크 쓰루풋**을 달성합니다. workers=4+에서 MPSC 제약으로 인해 성능이 크게 저하됩니다. 메일박스 크기 >= 8192는 프로듀서 차단을 방지하기 위해 중요합니다.

### 사용 시점

| 시나리오 | 권장사항 |
|---------|---------|
| 피크 쓰루풋 필요 | workers=1~2, maxBatch=8~16, 메일박스 >= 8192 |
| 무거운 핸들러 | workers=CPU 코어, maxBatch=32~64 |
| 메모리 제약 | 메일박스 = iterations/actors + 256 |

## 참고 문헌

- 소스: `src/core/perf/benchmark/mailbox_bench.cpp`
- 실행: `./build/v2_cli benchmark throughput [options]`
- 아키텍처: [메일박스 선택](../architecture/mailbox_comparison.md)
