# Throughput Benchmark

## 목적

**"이 시스템이 초당 메시지를 얼마나 빨리 처리할 수 있는가?"**

V2-Engine actor 시스템의 최대 메시지 처리량(throughput)을 측정합니다. 이 값은 시스템의 **성능 상한선**을 결정하는 핵심 지표입니다.

## 설계 원리

### 왜 이것이 중요한가?

 actor 시스템에서 메시지는 다음 단계를 거쳐 처리됩니다:

```
sendMsg() → Mailbox::push() → Dispatcher::dispatch() → Worker → Actor::handle()
```

각 단계마다 뮤텍스, 세마포어, 세마포어 등 동기화 비용이 발생합니다. 이 벤치마크는 **오버헤드만 격리**하여 측정합니다:

- `handle()`은 `atomic counter` 증가만 수행 (실제 연산 없음)
- 따라서 측정되는 것은 **동기화 인프라의 순수 처리 능력**

### 측정 방법

```
시간 측정 시작
  → N개 메시지를 M개 액터에 전달
  → W개 워커가 배치로 처리
  → 모든 메시지 처리 완료 대기
시간 측정 종료

처리량 = N / (종료 - 시작)
```

### 핵심 파라미터

| 파라미터 | 기본값 | 의미 |
|---------|-------|------|
| `--workers` | 4 | 워커 스레드 수 |
| `--actors` | 4 | 타겟 액터 수 |
| `--iterations` | 10,000 | 전송할 메시지 수 |
| `--maxbatch` | 32 | 워커당 최대 배치 크기 |
| `--warmup` | 0 | 워밍업 메시지 수 |
| `--mailbox` | 자동 | 메일박스 용량 (0=자동 계산) |

## 실험 결과

### 실험 1: 기본 설정

```
Config: workers=4, actors=4, iterations=50,000, maxbatch=32
Mailbox: 12,756 (자동)
Warmup: 1,000

Throughput: ~100,000 msgs/sec
Total Time: ~500 ms
```

**해석:**
- 4개 워커가 4개 액터의 메일박스에서 메시지를 배치로 꺼내 처리
- 초당 **약 10만 개** 메시지 처리 가능
- 1개 메시지당 평균 **~10μs** 소요 (enqueue + dispatch + handle)

### 실험 2: 워커 수에 따른 변화

| Workers | Throughput (msgs/sec) | 변화 |
|---------|----------------------|------|
| 1 | ~920,000 | 기준 |
| 2 | ~820,000 | -11% |
| 4 | ~136,000 | -85% |
| 8 | ~42,000 | -95% |

**해석:**
- 메시지 핸들러가 극히 가벼운 경우(원자적 증가만), **워커가 많을수록 느려짐**
- 원인: 세마포어 wake-up + 뮤프스 경쟁 + 컨텍스트 스위칭 비용 > 병렬화 이득
- **실제 애플리케이션**에서는 핸들러가 무거워지므로 다른 결과 예상

## 출력 예시

```
=== throughput ===
Measure message throughput

[Config]
  Workers:    4
  Actors:     4
  MaxBatch:   32
  Mailbox:    12,756
  Iterations: 50,000
  Warmup:     1,000

  Throughput: 100,123.45 msgs/sec
  Total Time: 499.38 ms
```

## 기술적 세부사항

### 동기화 경로

```
MainThread:  receiveMsg(Tick)
    └→ ActorContext::enqueue()
        ├→ Mailbox::push()           [mutex lock/unlock]
        └→ Dispatcher::dispatch()
            ├→ readyQueue_.push()    [mutex lock/unlock]
            └→ sema_.release()       [semaphore signal]

WorkerThread:  sema_.acquire()       [semaphore wait]
    └→ processBatch(maxBatch)
        └→ ActorContext::dequeue()
            └→ Mailbox::pop()        [mutex lock/unlock]
                └→ Actor::handle()
                    └→ counter_.fetch_add(1)
```

### 메일박스 용량 계산

`mailbox = 0`이면 자동으로 `iterations / actors + 256`으로 계산됩니다. 이는 각 액터가 받을 메시지 양 + 버퍼를 보장합니다.

## 결론

- 이 시스템의 **동기화 오버헤드만으로** 초당 ~10만 메시지 처리 가능
- 실제 핸들러가 I/O, DB, 계산 등을 수행하면 처리량은 하락
- **설계 참고치**: 핸들러가 1ms짜리 작업이면 ~1,000 msgs/sec 예상
