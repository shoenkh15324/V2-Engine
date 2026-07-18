# Contention Benchmark

## 목적

**"여러 스레드가 동시에 하나의 메일박스에 메시지를 넣을 때, 뮤텍스는 얼마나 빨리 처리할 수 있는가?"**

동시성(concurrency) 환경에서 뮤텍스 기반 메일박스의 **동시 push 처리 능력**을 측정합니다. 이 값은 **동시 메시지 주입 상한선**을 결정하는 핵심 지표입니다.

## 설계 원리

### 뮤텍스 경쟁(Mutex Contention)이란?

여러 스레드가 **하나의 뮤텍스**를 동시에 차지하려 할 때 발생하는 성능 저하입니다:

```
스레드 0: ──lock──[push]──unlock──
스레드 1:    ──lock──[push]──unlock──
스레드 2:       ──lock──[push]──unlock──
스레드 3:          ──lock──[push]──unlock──
                   ↑
             이 구간에서 스레드들이 대기함
```

### Throughput과의 차이

| | Throughput | Contention |
|---|---|---|
| 하는 일 | push() + **handle()** | push() **만** |
| 핸들링 | atomic counter 증가 | **없음** (워커는 소비만) |
| 측정 대상 | 전체 파이프라인 | **동시 push 속도만** |
| 프로듀서 | Main Thread 1개 | **N개 스레드** 동시 push |

### 왜 이것이 중요한가?

- **동시 주입 상한선 결정**: 시스템에 동시에 얼마나 많은 메시지를 주입할 수 있는지 파악
- **뮤텍스 병목 식별**: push()의 critical section이 얼마나 짧은지, 동시성을 얼마나 수용하는지
- **스케일링 특성 파악**: 프로듀서/워커 수를 늘렸을 때 실제로 빨라지는지 확인

### 측정 방법

```
1단계: 설정
   - W개 워커 스레드, P개 프로듀서 스레드
   - 1개 액터 생성, mailbox 크기 = iterations + 256 (자동 계산)

2단계: 동시 push (P개 스레드)
   - 각 프로듀서가 N/P개 메시지를 순차적으로 push
   - receiveMsg(Tick{}) → enqueue → push → dispatch 경로 traversal

3단계: 처리 대기
   - processed == N일 때까지 busy-wait (sleep 없음)
   - 30초 타임아웃 적용

4단계: 분석
   - throughput = N / (종료 - 시작)  [msgs/sec]
   - 이 값은 push + 소비의 end-to-end 시간
```

### 핵심 파라미터

| 파라미터 | 기본값 | 의미 |
|---------|-------|------|
| `--workers` | 4 | 워커 스레드 수 (소비자) |
| `--producers` | 8 | 프로듀서 스레드 수 (생산자) |
| `--iterations` | 100,000 | 총 메시지 수 |
| `--maxbatch` | 32 | 워커당 최대 배치 크기 |
| `--warmup` | 0 | 워밍업 메시지 수 |
| `--mailbox` | 자동 | 메일박스 용량 (0=iterations+256) |

## 실험 결과

### 테스트 환경

| 항목 | 사양 |
|------|------|
| CPU | AMD Ryzen 7 9800X3D (8코어) |
| RAM | 8 GB (WSL) |
| OS | Linux (WSL Ubuntu-22.04) |

**벤치마크 기본 파라미터:**

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| iterations | 100,000 | 총 전송 메시지 수 |
| warmup | 1,000 | 워밍업 메시지 수 |

### 실험 1: 프로듀서 수에 따른 변화

**조건**: workers=4, maxBatch=32, iterations=100,000

| Producers | Throughput (msgs/sec) | Total Time | 상대 성능 |
|-----------|----------------------|------------|----------|
| 1 | 789,414 | 126.7 ms | 기준 |
| 2 | 906,739 | 110.3 ms | +14.9% |
| 4 | 904,033 | 110.6 ms | +14.5% |
| 8 | 849,531 | 117.7 ms | +7.6% |
| 16 | 867,951 | 115.2 ms | +10.0% |
| 32 | 889,779 | 112.4 ms | +12.7% |

**분석:**
- **2~32개 프로듀서에서 거의 동일한 성능**: 85만~90만 msgs/sec
- **단일 프로듀서에서 약간 느림**: 멀티 프로듀서가 병렬 push로 이득
- **16개 이상에서 포화 없음**: 뮤텍스의 critical section(push)이 극히 짧아서 동시성 충분히 수용
- **핵심**: 뮤텍스 push는 ~100ns 수준으로 빠르므로, 프로듀서 수가 throughput에 큰 영향 없음

### 실험 2: 워커 수에 따른 변화

**조건**: producers=8, maxBatch=32, iterations=100,000

| Workers | Throughput (msgs/sec) | Total Time | 상대 성능 |
|---------|----------------------|------------|----------|
| 1 | 890,381 | 112.3 ms | 기준 |
| 2 | 875,811 | 114.2 ms | -1.6% |
| 4 | 862,723 | 115.9 ms | -3.1% |
| 8 | 841,516 | 118.8 ms | -5.5% |

**분석:**
- **워커 수가 throughput에 거의 영향 없음**: 모두 85만~89만 msgs/sec
- **약간의 감소 경향**: 워커가 많아지면 세마포어/뮤텍스 경쟁이 소폭 증가
- **핵심**: contention 벤치마크에서는 **push가 병목**이지 소비 속도가 아님
- **참고**: throughput 벤치마크와 달리, 핸들러가 가벼우므로 소비는 항상 push보다 빠름

### 실험 3: maxBatch에 따른 변화

**조건**: workers=4, producers=8, iterations=100,000

| maxBatch | Throughput (msgs/sec) | Total Time | 상대 성능 |
|----------|----------------------|------------|----------|
| 1 | 893,664 | 111.9 ms | 기준 |
| 4 | 882,401 | 113.3 ms | -1.3% |
| 8 | 886,176 | 112.8 ms | -0.8% |
| 16 | 876,217 | 114.1 ms | -1.9% |
| 32 | 868,641 | 115.1 ms | -2.8% |
| 64 | 848,016 | 117.9 ms | -5.1% |
| 128 | 848,518 | 117.9 ms | -5.1% |

**분석:**
- **maxBatch가 throughput에 거의 영향 없음**: 모두 85만~89만 msgs/sec
- **약간의 감소 경향**: maxBatch가 크면 워커가 더 오래 뮤텍스를 점유하지만, push 경쟁과 무관
- **핵심**: contention은 **push 측 뮤텍스 경쟁**이므로, pop 측 배치 크기는 영향 최소

### 실험 4: Mailbox 크기에 따른 변화

**조건**: workers=4, producers=8, maxBatch=32, iterations=100,000

| Mailbox | Throughput (msgs/sec) | Total Time | 비고 |
|---------|----------------------|------------|------|
| 256 | 3,333 | 30,007 ms | producer 대기 (30초 타임아웃) |
| 512 | 3,331 | 30,021 ms | producer 대기 (30초 타임아웃) |
| 8,192 | 3,332 | 30,009 ms | producer 대기 (30초 타임아웃) |
| 16,384 | 3,332 | 30,012 ms | producer 대기 (30초 타임아웃) |
| 32,768 | 3,321 | 30,111 ms | producer 대기 (30초 타임아웃) |
| 65,536 | 872,630 | 114.6 ms | 무블로킹 push |

**분석:**
- **Mailbox < 65,536에서 극단적으로 느림 (~3,300 msgs/sec)**: mailbox가 작으면 producer가 채우고 대기, worker의 pop 속도에 의존
- **Mailbox ≥ 65,536에서 급상승**: mailbox가 충분히 크면 producer가 블로킹 없이 메시지 주입 가능
- **Mailbox 크기 공식**: `iterations + 256`은 최소한의 안전 마진
- **핵심**: contention 측정을 위해서는 **mailbox가 충분히 커야 함** — 그래야 push 경쟁이 진짜 병목이 됨

### 실험 결과 종합

| 변수 | Throughput 변화 | 원인 |
|------|----------------|------|
| producers ↑ | **유지** (85만~90만) | 뮤텍스 push가 극히 빨라서 동시성 충분히 수용 |
| workers ↑ | **약간 감소** | 세마포어/뮤텍스 경쟁 소폭 증가 |
| maxBatch ↑ | **약간 감소** | pop 측 뮤텍스 점유 시간 증가 (push와 무관) |
| mailbox ↑ | **급증** (<65K:3.3k → ≥65K:87만) | producer blocking 해소 |

## 출력 예시

```
=== contention ===
Measure multi-producer contention performance

[Test Config]
  Workers:    4
  Actors:     1
  MaxBatch:   32
  Mailbox:    100256
  Iterations: 100000
  Warmup:     1000

  Throughput: 889,778.84 msgs/sec
  Total Time: 112.39 ms
```

## 기술적 세부사항

### 뮤텍스와 세마포어의 역할

```
프로듀서 0 ─┐
프로듀서 1 ─┤
프로듀서 2 ─┼─→ ActorContext::enqueue()
프로듀서 3 ─┤     ├→ Mailbox::push()        [뮤텍스 lock/unlock] ← 첫 번째 뮤텍스
프로듀서 4 ─┤     └→ Dispatcher::dispatch()
프로듀서 5 ─┤          ├→ readyQueue_.push() [뮤텍스 lock/unlock] ← 두 번째 뮤텍스
프로듀서 6 ─┤          └→ sema_.release()     [세마포어 시그널]
프로듀서 7 ─┘

Worker 스레드:
  sema_.acquire()  [세마포어 웨이트]
    └→ ActorContext::run(maxBatch)
         └→ Mailbox::pop()   [뮤텍스 lock/unlock] ← 세 번째 뮤텍스
              └→ Actor::handle()
                   └→ counter_.fetch_add(1)  [원자적 연산]
```

### push의 critical section 분석

```cpp
// Mailbox::push - 고정 크기 링 버퍼
template <typename U>
bool push(U&& msg){
    std::lock_guard<std::mutex> lock(mutex_);    // ← 뮤텍스 획득
    if(count_ == capacity_) return false;         //    (약 20-50ns)
    buffer_[tail_] = std::forward<U>(msg);       //    메시지 복사
    tail_ = (tail_ + 1) % capacity_;             //    포인터 갱신
    ++count_;                                     //    카운트 증가
    return true;                                  //    unlock 포함
}
```

- **critical section**: lock + 복사 + 포인터 갱신 + unlock ≈ **50~100ns**
- **단일 스레드 push 처리량**: ~10만~20만 msgs/sec (100ns/push 기준)
- **동시 push**: 뮤텍스 해제 즉시 다른 스레드가 획득 → 거의 선형 스케일링 가능

### 동시 push의 안전성

`Mailbox::push()`는 뮤텍스로 보호됩니다:
- 한 번에 하나의 프로듀서만 push 가능
- 뮤텍스 해제 후 다른 프로듀서가 즉시 push
- 메일박스 데이터 일관성 보장
- **단, lock 경쟁으로 인한 컨텍스트 스위칭 비용 발생**

### 왜 contention에서는 throughput보다 빠른가?

throughput 벤치마크와 동일한 단일 프로듀서 조건에서:
- **throughput**: push + dispatch + sema + pop + handle (전체 경로)
- **contention**: push + dispatch + sema + pop + handle (동일 경로)

하지만 contention의 기본 설정은 **8개 프로듀서**입니다:
- 8개 스레드가 동시에 push → 뮤텍스 대기 시간 최소화 (pipeline 효과)
- throughput의 단일 프로듀서는 push 후 handle까지 기다림

### 뮤텍스 경쟁 포화점

실험 결과, 뮤텍스 경쟁은 **예상보다 빨리 포화되지 않음**:
- critical section이 ~100ns로 극히 짧음
- 컨텍스트 스위칭 비용(~1μs)보다 짧지만, 8개 스레드 정도는 충분히 수용
- **16~32개 프로듀서에서도 포화 없음**: modern CPU의 뮤텍스 구현이 효율적

## 결론

### 핵심 발견

1. **동시 push 처리량은 85만~90만 msgs/sec**
   - 8개 프로듀서에서 안정적으로 이 수준 유지
   - 프로듀서 수를 늘려도 크게 변하지 않음

2. **뮤텍스 push는 극히 빠름 (~100ns)**
   - critical section이 짧아서 동시성 충분히 수용
   - 16~32개 프로듀서에서도 경쟁 포화 없음

3. **Mailbox 크기가 contention 측정에 가장 큰 영향**
   - Mailbox < 65,536: producer blocking으로 인한 극단적 느림 (~3,300 msgs/sec)
   - Mailbox ≥ 65,536: 무블로킹 push로 진짜 contention 측정 가능
   - **Mailbox는 최소 `iterations + 256` 이상 설정 필요**

4. **Worker 수는 contention에 거의 영향 없음**
   - push가 병목이므로 소비 속도와 무관
   - 워커가 많아지면 오히려 세마포어 경쟁으로 약간 느려짐

5. **maxBatch도 contention에 거의 영향 없음**
   - pop 측 배치 크기는 push 경쟁과 무관
   - 약간의 감소 경향만 존재

6. **Throughput 벤치마크와의 관계**
   - contention ~87만 msgs/sec ≈ throughput 최고 성능(~87만)과 유사
   - 이는 단일 프로듀서의 push + handle 처리량과 일치
   - **동시 push는 throughput에 큰 영향 없음** (뮤텍스가 충분히 빠름)

### 권장 사항

1. **Mailbox 크기 선택**:
   - contention 측정 시mailbox는 충분히 크게 설정 (≥ iterations + 256)
   - 작은 mailbox는 producer blocking으로 인해 진짜 contention 측정 불가

2. **프로듀서 수 조정**:
   - 기본 8개로 충분 (포화점에 도달하지 않음)
   - 실제 시스템의 동시 push 수준에 맞춰 조절

3. **웍커 수 조정**:
   - contention 측정에서는 workers=1~4로 충분
   - workers가 많아져도 push 경쟁에 영향 없음

4. **maxBatch 설정**:
   - contention 측정에서는 maxBatch=1~32로 충분
   - push 경쟁과 무관하므로 자유롭게 설정 가능
