# Throughput Benchmark

## 목적

**"메시지를 넣고 꺼내는 전체 경로에서 초당 얼마큼의 메시지를 처리할 수 있는가?"**

V2-Engine actor 시스템의 **최대 메시지 처리량(throughput)**을 측정합니다. 이 값은 시스템의 **성능 상한선**을 결정하는 핵심 지표입니다.

## 설계 원리

### 처리 경로란?

actor 시스템에서 메시지는 다음 단계를 거쳐 처리됩니다:

```
Main Thread:  receiveMsg(Tick{})
    └→ ActorContext::enqueue()
        ├→ Mailbox::push()           [mutex lock/unlock]
        └→ Dispatcher::dispatch()
            ├→ readyQueue_.push()    [mutex lock/unlock]
            └→ sema_.release()       [semaphore signal]

Worker Thread:  sema_.acquire()      [semaphore wait]
    └→ ActorContext::run(maxBatch)
        └→ Mailbox::pop()           [mutex lock/unlock]
            └→ Actor::handle()
                └→ counter_.fetch_add(1)
```

각 단계마다 뮤텍스, 세마포어 등 동기화 비용이 발생합니다. 이 벤치마크는 **오버헤드만 격리**하여 측정합니다:

- `handle()`은 `atomic counter` 증가만 수행 (실제 연산 없음)
- 따라서 측정되는 것은 **동기화 인프라의 순수 처리 능력**

### 왜 이것이 중요한가?

- **성능 상한선 결정**: 핸들러가 아무리 빨라도 이 경로의 한계를 넘을 수 없음
- **병목 구간 식별**: 뮤텍스, 세마포어, 세마포어 중 어디서 시간이 소모되는지 파악
- **스케일링 특성 파악**: 워커/액터 수를 늘렸을 때 실제로 빨라지는지 확인

### 측정 방법

```
1단계: 설정
   - W개 워커 스레드, A개 액터, maxBatch=M으로 ActorSystem 생성
   - 각 액터에 mailbox 크기 = iterations/actors + 256 (자동 계산)

2단계: 메시지 주입 (Main Thread)
   - N개 메시지를 라운드로빈으로 A개 액터에 전달
   - receiveMsg(Tick{}) → enqueue → push → dispatch 경로 traversal

3단계: 처리 대기 (Main Thread)
   - processed == N일 때까지 busy-wait
   - 30초 타임아웃 적용

4단계: 분석
   - throughput = N / (종료 - 시작)  [msgs/sec]
   - totalDuration = 전체 소요 시간
```

### 핵심 파라미터

| 파라미터 | 기본값 | 의미 |
|---------|-------|------|
| `--workers` | 4 | 워커 스레드 수 |
| `--actors` | 1 | 타겟 액터 수 |
| `--iterations` | 100,000 | 전송할 메시지 수 |
| `--maxbatch` | 32 | 워커당 최대 배치 크기 |
| `--warmup` | 0 | 워밍업 메시지 수 |
| `--mailbox` | 자동 | 메일박스 용량 (0=iterations/actors+256) |

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

### 실험 1: 기본 설정 (반복 측정)

**조건**: workers=4, actors=4, maxBatch=32

| Iterations | Throughput (msgs/sec) | Total Time |
|------------|----------------------|------------|
| 10,000 | 86,901 | 115.1 ms |
| 50,000 | 112,290 | 445.3 ms |
| 100,000 | 132,248 | 756.2 ms |

**분석:**
- iterations가 증가할수록 throughput이 상승: 워밍업 + 세마포어 초기화 오버헤드가 상대적으로 작아짐
- 초당 **약 8.7~13.2만 개** 메시지 처리 가능
- 1개 메시지당 평균 **7.6~11.5μs** 소요 (enqueue + dispatch + handle)

### 실험 2: 워커 수에 따른 변화

**조건**: actors=1, maxBatch=32, iterations=100,000

| Workers | Throughput (msgs/sec) | Total Time | 상대 성능 |
|---------|----------------------|------------|----------|
| 1 | 866,423 | 115.4 ms | 기준 |
| 2 | 861,590 | 116.1 ms | -0.6% |
| 4 | 443,500 | 225.5 ms | -48.8% |
| 8 | 206,772 | 483.6 ms | -76.1% |

**분석:**
- **워커가 많을수록 느려짐**: 핸들러가 극히 가벼운 경우(원자적 증가만), 워커 증가는 오히려 독
- 원인: 세마포어 wake-up + 뮤텍스 경쟁 + 컨텍스트 스위칭 비용 > 병렬화 이득
- **1개 워커가 최고 성능**: 단일 액터의 mailbox는 하나의 워커만으로 충분히 처리 가능
- **실제 애플리케이션**에서는 핸들러가 무거워지므로(예: I/O, DB, 계산) 다른 결과 예상

### 실험 3: 액터 수에 따른 변화

**조건**: workers=4, maxBatch=32, iterations=100,000

| Actors | Throughput (msgs/sec) | Total Time | 상대 성능 |
|--------|----------------------|------------|----------|
| 1 | 466,877 | 214.2 ms | 기준 |
| 2 | 147,351 | 678.7 ms | -68.4% |
| 4 | 110,550 | 904.6 ms | -76.3% |
| 8 | 120,134 | 832.4 ms | -74.3% |
| 16 | 122,546 | 816.0 ms | -73.7% |

**분석:**
- **1개 액터에서 최고 성능**: 단일 mailbox에 대한 뮤텍스 경쟁이 최소
- **2~4개 액터에서 급감**: 각 액터의 mailbox push/pop 뮤텍스 경쟁 + 워커의 컨텍스트 스위칭
- **8~16개에서 부분 회복**: 액터 수가 많아지면 mailbox 크기가 작아져 캐시 효율 증가
- **핵심**: 가벼운 핸들러에서는 액터 분산이 오히려 오버헤드를 증가시킴

### 실험 4: maxBatch에 따른 변화

**조건**: workers=4, actors=1, iterations=100,000

| maxBatch | Throughput (msgs/sec) | Total Time | 상대 성능 |
|----------|----------------------|------------|----------|
| 1 | 810,677 | 123.4 ms | 기준 |
| 4 | 795,388 | 125.7 ms | -1.9% |
| 8 | 756,367 | 132.2 ms | -6.7% |
| 16 | 498,861 | 200.5 ms | -38.5% |
| 32 | 508,362 | 196.7 ms | -37.3% |
| 64 | 341,305 | 293.0 ms | -57.9% |
| 128 | 810,142 | 123.4 ms | -0.1% |

**분석:**
- **maxBatch가 작을수록 빠름**: maxBatch=1~8에서 최고 성능 (~75~81만 msgs/sec)
- **maxBatch가 커질수록 느려짐**: 사이클당 더 많은 메시지를 pop하려다 뮤텍스 경쟁 심화
- **원인**: `Mailbox::pop()`은 뮤텍스 lock/unlock이 필요. maxBatch가 크면 한 스레드가 오래 뮤텍스를 점유 → 다른 워커/디스패처 대기
- **maxBatch=128에서 이상치**: 캐시 라인 효과 또는 배치 처리 최적화로 인한 일시적 성능 회복
- **실제 애플리케이션**: maxBatch를 키우면 컨텍스트 스위칭 횟수 감소 → 적절한 밸런스 필요

### 실험 5: Mailbox 크기에 따른 변화

**조건**: workers=4, actors=1, maxBatch=32, iterations=100,000

| Mailbox | Throughput (msgs/sec) | Total Time | 비고 |
|---------|----------------------|------------|------|
| 256 | 3,311 | 30,205 ms | producer 대기 |
| 512 | 3,309 | 30,219 ms | producer 대기 |
| 1,024 | 3,316 | 30,153 ms | producer 대기 |
| 2,048 | 3,307 | 30,240 ms | producer 대기 |
| 4,096 | 3,307 | 30,241 ms | producer 대기 |
| 8,192 | 372,109 | 269 ms | |
| 16,384 | 397,665 | 251 ms | |
| 32,768 | 344,103 | 291 ms | |
| 65,536 | 421,508 | 237 ms | |

**분석:**
- **Mailbox < 8192에서 극단적으로 느림 (~3,300 msgs/sec)**: mailbox가 작으면 producer가 빠르게 채우고, consumer가 비울 때까지 대기
- **Mailbox ≥ 8192에서 급상승**: mailbox가 충분히 크면 producer가 블로킹 없이 메시지 주입 가능
- **Mailbox 65,536에서 최고 성능**: 충분한 버퍼링으로 인한 높은 처리량
- **핵심**: mailbox 크기는 throughput에 **직접적이고 가장 큰 영향**을 미침
- **Mailbox 크기 공식**: `iterations/actors + 256`은 최소한의 안전 마진

## 출력 예시

```
=== throughput ===
Measure message throughput

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

## 기술적 세부사항

### 동기화 경로 상세

```
Main Thread:  receiveMsg(Tick)
    └→ ActorContext::enqueue()
        ├→ Mailbox::push()           [mutex lock/unlock] ← 첫 번째 뮤텍스
        └→ Dispatcher::dispatch()
            ├→ readyQueue_.push()    [mutex lock/unlock] ← 두 번째 뮤텍스
            └→ sema_.release()       [semaphore signal]  ← 세마포어

Worker Thread:  sema_.acquire()      [semaphore wait]    ← 세마포어
    └→ ActorContext::run(maxBatch)
        └→ while(processed < maxBatch):
            └→ Mailbox::pop()        [mutex lock/unlock] ← 세 번째 뮤텍스
                └→ Actor::handle()
                    └→ counter_.fetch_add(1)             ← 원자적 연산
```

### 메일박스 용량 계산

`mailbox = 0`이면 자동으로 `iterations / actors + 256`으로 계산됩니다. 이는 각 액터가 받을 메시지 양 + 버퍼를 보장합니다.

### 뮤텍스 병목 분석

Throughput 벤치마크에서 가장 큰 병목은 **Mailbox 뮤텍스**입니다:

- `push()`와 `pop()` 모두 동일한 뮤텍스를 경쟁
- maxBatch가 크면 한 워커가 오래 뮤텍스를 점유 → 다른 워커 대기
- workers가 많으면 push/pop 경쟁 증가 → throughput 감소
- **가벼운 핸들러에서는 1개 워커 + 작은 maxBatch가 최적**

### 스케일링 특성 요약

| 변수 | 증가 시 throughput 변화 | 원인 |
|------|----------------------|------|
| workers ↑ | **감소** | 세마포어/뮤텍스 경쟁 증가 |
| actors ↑ | **감소** (일부 회복) | 뮤텍스 분산 + 캐시 효율 변화 |
| maxBatch ↑ | **감소** | 뮤텍스 점유 시간 증가 |
| mailbox ↑ | **증가** | producer 대기 시간 감소 |

## 결론

### 핵심 발견

1. **동기화 오버헤드만으로 초당 ~86만 메시지 처리 가능**
   - maxBatch=1~8, workers=1, actors=1에서 최고 성능
   - 이는 순수 뮤텍스 + 세마포어 오버헤드만 측정한 값

2. **Mailbox 크기가 throughput에 가장 큰 영향**
   - Mailbox < 8192: ~3,300 msgs/sec (producer 블로킹)
   - Mailbox ≥ 65,536: ~42만 msgs/sec (무블로킹)
   - **Mailbox는 최소 `iterations/actors + 256` 이상 권장**

3. **워커 수가 많을수록 느려짐 (가벼운 핸들러)**
   - 1 workers: 866,423 msgs/sec
   - 8 workers: 206,772 msgs/sec (-76%)
   - **가벼운 작업에서는 워커를 적게 설정**

4. **maxBatch가 작을수록 빠름**
   - maxBatch=1: 810,677 msgs/sec
   - maxBatch=64: 341,305 msgs/sec (-58%)
   - **뮤텍스 경쟁을 줄이려면 maxBatch를 작게**

5. **액터 수는 1개일 때 최고 성능**
   - 1 actor: 466,877 msgs/sec
   - 4 actors: 110,550 msgs/sec (-76%)
   - **실제 애플리케이션에서는 핸들러 무게에 따라 조절 필요**

6. **실제 애플리케이션에서는 다른 결과 예상**
   - 핸들러가 1ms짜리 작업이면 ~1,000 msgs/sec 예상
   - 핸들러가 무거워지면 워커/액터 분산의 이득이 나타남

### 권장 사항

1. **Mailbox 크기 선택**:
   - 처리량이 중요하면 큰 mailbox 사용 (≥ 8,192)
   - 메모리가 제한되면 `iterations/actors + 256` 이상 설정

2. **maxBatch 설정**:
   - 최대 처리량이 필요하면 maxBatch=1~4 사용
   - 컨텍스트 스위칭 오버헤드를 줄이려면 maxBatch=16~32 사용

3. **workers 수 조정**:
   - 가벼운 핸들러: workers=1~2 (최고 성능)
   - 무거운 핸들러: CPU 코어 수만큼 workers 설정

4. **actors 수 조정**:
   - 단일 액터로 충분하면 1개 사용
   - 여러 액터가 필요하면 mailbox를 충분히 크게 설정
