# 경합 벤치마크

## 목적

> **연구 질문:** "여러 스레드가 동시에 하나의 메일박스에 얼마나 빠르게 push할 수 있는가?"

**동시 프로듀서 경합 하에서의 push 쓰루풋**을 측정합니다. 이는 **최대 동시 메시지 주입 속도**를 결정합니다.

## 설계 원칙

### 왜 중요한가

실제 시스템에서 여러 스레드가 동시에 같은 액터에게 메시지를 보내야 할 때가 많습니다. 이 벤치마크는 **push 경로**를 격리하여 메일박스가 동시 쓰기를 얼마나 잘 처리하는지 측정합니다.

### 측정 방법

```
1. Setup:    W workers, P producers, 1 actor
2. Execute:  P개 스레드가 각각 N/P개 메시지 push (동시)
3. Wait:     processed == N until (30초 타임아웃)
4. Analyze:  throughput = N / elapsed [msgs/sec]
```

쓰루풋 벤치마크와 달리 경합 벤치마크에는 **여러 프로듀서 스레드**가 동시에 push합니다.

### 파라미터 참조

| 파라미터 | 기본값 | 설명 |
|---------|--------|------|
| `--workers` | 4 | 워커 스레드 수 (컨슈머) |
| `--producers` | 8 | 프로듀서 스레드 수 |
| `--iterations` | 100,000 | 전체 메시지 수 |
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
| 도구 | `./build/v2_cli benchmark contention` |
| 측정 방식 | Busy-wait (no sleep, 30초 타임아웃) |

## 실험 결과

### 기본 파라미터

| 파라미터 | 값 | 설명 |
|---------|-----|------|
| iterations | 100,000 | 전체 메시지 수 |
| warmup | 1,000 | 웜업 메시지 수 |

### 실험 1: 프로듀서 수

**조건:** workers=4, maxBatch=32, iterations=100,000

| Producers | 쓰루풋 (msgs/sec) | 비고 |
|-----------|-------------------|------|
| 1 | 104,005 | 단일 프로듀서 |
| 2 | **7,966,932** | 피크 |
| 4 | 7,061,879 | |
| 8 | 6,159,564 | |
| 16 | 4,628,588 | |
| 32 | 5,219,337 | |

**분석:**
- **프로듀서 2개에서 피크**: 7.97M msgs/sec
- **프로듀서 수 증가에 따라 약간 감소**: readyQueue 뮤텍스 경쟁
- **핵심:** LockFreeMailbox는 push 경로에서 **높은 동시성** 달성

### 실험 2: 워커 수

**조건:** producers=8, maxBatch=32, iterations=100,000

| Workers | 쓰루풋 (msgs/sec) | 비고 |
|---------|-------------------|------|
| 1 | **7,000,579** | 피크 |
| 2 | 6,153,322 | |
| 4 | 6,159,564 | |
| 8 | 4,716,703 | |

**분석:**
- **workers=1에서 피크**: 7.0M msgs/sec
- **워커 수 증가에 따라 약간 감소**: readyQueue/디스패처 뮤텍스 경쟁 증가
- **핵심:** 경합은 push 바인드, 소비 바인드가 아님

### 실험 3: maxBatch

**조건:** workers=4, producers=8, iterations=100,000

| maxBatch | 쓰루풋 (msgs/sec) | 비고 |
|----------|-------------------|------|
| 1 | 2,083,956 | |
| 32 | **6,159,564** | 피크 |

**분석:**
- **maxBatch=32**: 6.16M msgs/sec — 배치 처리가 쓰루풋 증폭
- **maxBatch=1**: 2.08M msgs/sec — 단일 메시지 처리에서도 이점
- **핵심:** 경합은 push 바인드 → LockFreeMailbox가 지배적

### 실험 4: 메일박스 크기

**조건:** workers=4, producers=8, maxBatch=32, iterations=100,000

| 메일박스 | 쓰루풋 (msgs/sec) | 전체 시간 | 비고 |
|---------|-------------------|----------|------|
| 256 | 3,333 | 30,007 ms | 프로듀서 차단 (30초 타임아웃) |
| 512 | 3,331 | 30,021 ms | 프로듀서 차단 (30초 타임아웃) |
| 8,192 | 3,332 | 30,009 ms | 프로듀서 차단 (30초 타임아웃) |
| 16,384 | 3,332 | 30,012 ms | 프로듀서 차단 (30초 타임아웃) |
| 32,768 | 3,321 | 30,111 ms | 프로듀서 차단 (30초 타임아웃) |
| 65,536 | **872,630** | 114.6 ms | 비차단 push |

**분석:**
- **메일박스 < 65,536**: ~3,300 msgs/sec — 프로듀서가 컨슈머 대기
- **메일박스 >= 65,536**: 872K+ msgs/sec — 비차단 push로 진짜 경합 가능
- **핵심:** 진짜 경합을 측정하려면 메일박스가 **충분히 커야 함**

### 결과 요약

| 변수 | 쓰루풋 변화 | 원인 |
|------|------------|------|
| producers ↑ | **안정적** (5M~8M) | 락프리 push가 잘 스케일 |
| workers ↑ | **약간 감소** | readyQueue/디스패처 경쟁 |
| maxBatch ↑ | **증가** (2M→6M) | 배치 처리가 쓰루풋 증폭 |
| mailbox ↑ | **급증** (<65K:3.3K → >=65K:872K) | 프로듀서 차단 해소 |

## 출력 예시

```
=== contention ===
멀티 프로듀서 경합 성능 측정

[Test Config]
  Workers:    4
  Actors:     1
  MaxBatch:   32
  Mailbox:    100256
  Iterations: 100000
  Warmup:     1000

  Throughput: 5,219,337 msgs/sec
  Total Time: 19.16 ms
```

## 기술적 상세

### 락프리 push 메커니즘

```cpp
// LockFreeMailbox::push (CAS 기반)
template <typename U>
bool push(U&& msg){
    while(true){
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % capacity_;
        if(next_tail == head_.load(std::memory_order_acquire)){
            return false; // 가득 참
        }
        if(tail_.compare_exchange_weak(current_tail, next_tail)){
            buffer_[current_tail] = std::forward<U>(msg);
            return true;
        }
    }
}
```

- **뮤텍스 없음**: CAS 기반 락프리 push
- **동시 push**: 여러 프로듀서가 동시에 push 가능
- **단일 컨슈머**: 하나의 컨슈머만 허용 (MPSC)

### 경합 벤치마크가 높은 쓰루풋을 보이는 이유

8개 프로듀서에서는 락프리 push가 경합을 최소화합니다. 쓰루풋 벤치마크는 단일 프로듀서를 사용하며, 전체 경로(push → handle → ack)를 기다려야 합니다.

## 분석

### 핵심 발견

1. **피크 쓰루풋: 프로듀서 2개에서 7.97M msgs/sec**
   - 락프리 push가 동시성에서 잘 스케일

2. **워커 수 증가에 따라 약간 감소**
   - readyQueue/디스패처 뮤텍스 경쟁 증가
   - **경합 시나리오에서는 workers=1~4 최적**

3. **maxBatch가 쓰루풋 증폭**
   - maxBatch=32: 6.16M msgs/sec (maxBatch=1 대비 3배)
   - **최대 쓰루풋을 위해 maxBatch=32 사용**

4. **메일박스 크기가 중요**
   - 메일박스 < 65,536: 프로듀서 차단 지배
   - 메일박스 >= 65,536: 진짜 경합 관찰 가능
   - **메일박스 >= iterations + 256 설정**

### 권장사항

| 우선순위 | 권장사항 | 예상 효과 |
|---------|---------|----------|
| 높음 | producers=2~8로 피크 쓰루풋 | 5M~8M msgs/sec |
| 중간 | maxBatch=32 설정 | 쓰루풋 3배 증가 |
| 중간 | 메일박스 >= iterations + 256 설정 | 진짜 경합 측정 |
| 낮음 | workers=1~4 최적 | readyQueue 경쟁 최소화 |

## 결론

### 요약

LockFreeMailbox는 멀티 프로듀서 시나리오에서 **7.97M msgs/sec 피크 쓰루풋**을 달성합니다. 락프리 push 메커니즘이 동시성에서 잘 스케일하지만, 워커 수 증가에 따라 readyQueue 경쟁으로 인해 쓰루풋이 약간 감소합니다.

### 사용 시점

| 시나리오 | 권장사항 |
|---------|---------|
| 여러 스레드가 같은 액터에게 push | LockFreeMailbox |
| 높은 동시성 필요 | producers=2~8, maxBatch=32 |
| 실시간 이벤트 처리 | 메일박스 >= 65,536 |

## 참고 문헌

- 소스: `src/core/perf/benchmark/mailbox_multithread_bench.cpp`
- 실행: `./build/v2_cli benchmark contention [options]`
