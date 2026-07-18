# Scaling Benchmark

## 목적

**"워커를 늘리면(또는 액터를 늘리면) 성능이 선형적으로 좋아지는가?"**

actor 시스템의 **스케일링 효율성**을 측정합니다. 이상적인 병렬 시스템은 워커가 2배이면 성능도 2배여야 하지만, 현실은 그렇지 않습니다.

## 설계 원리

### 스케일링이 중요한 이유

분산/병렬 시스템을 설계할 때 핵심 질문:

- **워커 스케일링**: CPU 코어를 더 쓰면 더 빨라지는가?
- **액터 스케일링**: 액터를 더 만들면 더 빨라지는가?

이상적인 스케일링:
```
Workers: 1 → 2 → 4 → 8
Throughput: 100K → 200K → 400K → 800K (선형)
Efficiency: 1.0x → 1.0x → 1.0x → 1.0x
```

현실적인 스케일링 (경량 작업):
```
Workers: 1 → 2 → 4 → 8
Throughput: 920K → 660K → 105K → 55K (역감소!)
Efficiency: 1.0x → 0.36x → 0.03x → 0.01x
```

### 측정 방법

**테스트 1: Worker Scaling** (액터 수 고정)
```
for each worker_count in [1, 2, 4, 8]:
    throughput = measure(worker_count, fixed_actors, iterations)
```

**테스트 2: Actor Scaling** (워커 수 고정)
```
for each actor_count in [1, 2, 4, 8]:
    throughput = measure(fixed_workers, actor_count, iterations)
```

**효율 계산**:
```
baseThroughput = throughput(1 worker/actor)
efficiency(N) = throughput(N) / (N × baseThroughput)
```

### 왜 스케일링이 안 되는가? (경량 작업의 경우)

핸들러가 `atomic counter 증가`만 하는 경우:

```
핸들링 비용:  ~5ns  (원자적 증가)
동기화 비용: ~500ns (뮤텍스 + 세마포어 + 컨텍스트 스위칭)
```

동기화 비용이 핸들링 비용의 **100배**. 따라서:
- 워커가 늘어나면 동기화 비용이 **기하급수적으로** 증가
- 병렬화의 이득보다 동기화의 비용이 큼

## 실험 결과

### 테스트 환경

| 항목 | 사양 |
|------|------|
| CPU | AMD Ryzen 7 9800X3D (8코어) |
| RAM | 8 GB (WSL) |
| OS | Linux (WSL Ubuntu-22.04) |
| 측정 방식 | Busy-wait (sleep 없이 순수 spin-wait, 30초 타임아웃) |

**벤치마크 기본 파라미터:**

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| iterations | 100,000 | 총 전송 메시지 수 |
| maxBatch | 32 | 워커당 최대 배치 크기 |
| warmup | 1,000 | 워밍업 메시지 수 |

### 실험 1: Worker Scaling (actors 고정)

#### actors=1 고정

| Workers | Throughput (msgs/sec) | Efficiency |
|---------|----------------------|------------|
| 1 | 902,549 | 1.00x |
| 2 | 918,167 | 0.51x |
| 4 | 580,459 | 0.16x |
| 8 | 333,845 | 0.05x |

#### actors=4 고정

| Workers | Throughput (msgs/sec) | Efficiency |
|---------|----------------------|------------|
| 1 | 923,388 | 1.00x |
| 2 | 663,808 | 0.36x |
| 4 | 105,374 | 0.03x |
| 8 | 55,214 | 0.01x |

#### actors=8 고정

| Workers | Throughput (msgs/sec) | Efficiency |
|---------|----------------------|------------|
| 1 | 914,811 | 1.00x |
| 2 | 706,816 | 0.39x |
| 4 | 101,502 | 0.03x |
| 8 | 36,444 | 0.00x |

**분석:**
- **워커가 많을수록 느려짐**: 모든 액터 수에서 동일한 패턴
- **actors=1일 때 가장 완만한 감소**: 단일 mailbox라 뮤텍스 경쟁 최소
- **actors=4~8일 때 급격한 감소**: 여러 mailbox의 push/pop이 뮤텍스를 경쟁
- **8 workers에서 0.00~0.05x**: 사실상 스케일링 실패
- **핵심**: 경량 핸들러에서는 **동기화 비용이 병렬화 이득을 압도**

### 실험 2: Actor Scaling (workers 고정)

#### workers=1 고정

| Actors | Throughput (msgs/sec) | Efficiency |
|--------|----------------------|------------|
| 1 | 858,987 | 1.00x |
| 2 | 913,994 | 0.53x |
| 4 | 902,551 | 0.26x |
| 8 | 920,070 | 0.13x |

#### workers=4 고정

| Actors | Throughput (msgs/sec) | Efficiency |
|--------|----------------------|------------|
| 1 | 331,328 | 1.00x |
| 2 | 123,574 | 0.19x |
| 4 | 91,197 | 0.07x |
| 8 | 97,628 | 0.04x |

#### workers=8 고정 (추가 실험 없음, actors=8 기준)

| Actors | Throughput (msgs/sec) | Efficiency |
|--------|----------------------|------------|
| 1 | 387,820 | 1.00x |
| 2 | 140,220 | 0.18x |
| 4 | 124,535 | 0.08x |
| 8 | 88,105 | 0.03x |

**분석:**
- **workers=1에서 액터 스케일링이 작동**: 1→8 액터에서 throughput 유지 (~900K)
  - 단일 워커는 하나의 mailbox만 처리하므로 뮤텍스 경쟁 없음
  - 액터가 늘어도 각 액터의 mailbox가 독립적이므로 영향 없음
- **workers=4~8에서 액터 스케일링 실패**: 1→8 액터에서 70~90% 감소
  - 여러 워커가 여러 mailbox를 동시에 pop → 뮤텍스 경쟁 폭발
- **핵심**: **단일 워커에서는 액터 수가 throughput에 거의 영향 없음**

### 스케일링 패턴 종합

| 변수 | workers=1 | workers=4 | 원인 |
|------|----------|----------|------|
| Worker ↑ | **감소** (0.05x@8) | **급감** (0.01x@8) | 세마포어/뮤텍스 경쟁 |
| Actor ↑ | **유지** (~900K) | **급감** (0.04x@8) | 뮤텍스 경쟁 (워커 수에 의존) |

### 스케일링 곡선 시각화

```
Throughput (workers=1 고정, actors 스케일링)
  ↑
  │ ★ 859K  ★ 914K  ★ 903K  ★ 920K
  │ ──────────────────────────────── (선형 유지)
  └──────────────────────────────────→ Actors
      1     2     4     8

Throughput (actors=4 고정, workers 스케일링)
  ↑
  │ ★ 923K
  │
  │   ★ 664K
  │
  │
  │              ★ 105K
  │
  │                     ★ 55K
  └──────────────────────────────────→ Workers
      1     2     4     8
```

## 출력 예시

```
=== scaling ===
Measure worker and actor scaling efficiency

[Test Config]
  Workers:    4
  Actors:     4
  MaxBatch:   32
  Mailbox:    0
  Iterations: 100000
  Warmup:     1000

  Throughput: 923387.90 msgs/sec
  Total Time: 108.30 ms

[Worker Scaling]
  1 workers: 923388 m/s (1.00x)
  2 workers: 663808 m/s (0.36x)
  4 workers: 105374 m/s (0.03x)
  8 workers: 55214 m/s (0.01x)

[Actor Scaling]
  1 actors: 331328 m/s (1.00x)
  2 actors: 123574 m/s (0.19x)
  4 actors: 91197 m/s (0.07x)
  8 actors: 97628 m/s (0.04x)
```

## 기술적 세부사항

### Why single worker is fastest

단일 워커의 장점:
- 세마포어 경쟁 없음 (유일한 워커가 모든 wake-up을 처리)
- 뮤텍스 contention 없음 (하나의 워커만 pop)
- 캐시 locality 극대화 (한 스레드가 모든 데이터를 처리)
- 컨텍스트 스위칭 없음 (스케줄러가 한 워커만 관리)

### Why actor scaling works with workers=1

단일 워커에서는:
1. Main Thread가 actor A의 mailbox에 push
2. Worker가 actor A의 mailbox에서 pop → handle
3. Main Thread가 actor B의 mailbox에 push
4. Worker가 actor B의 mailbox에서 pop → handle

**각 mailbox의 뮤텍스를 하나의 워커만 사용**하므로 경쟁 없음. 액터가 늘어도 동일한 워커가 순차적으로 처리하므로 throughput 유지.

### Why actor scaling fails with workers>1

여러 워커에서는:
1. Worker 1이 actor A의 mailbox pop 시도
2. Worker 2가 actor A의 mailbox pop 시도 → **뮤텍스 경쟁**
3. Worker 3이 actor B의 mailbox pop 시도
4. Worker 4가 actor B의 mailbox pop 시도 → **뮤텍스 경쟁**

**여러 워커가 하나의 mailbox를 동시에 pop하려 하면** 뮤텍스 경쟁이 발생. 액터가 많아질수록 경쟁 기회 증가.

### 동기화 비용 분해

```
세마포어 acquire/release: ~200-500ns
  └→ 워커 wake-up + 컨텍스트 스위칭
뮤텍스 lock/unlock: ~20-50ns
  └→ Mailbox::push/pop
dispatch (readyQueue push): ~20-50ns
  └→ Dispatcher::dispatch

총 동기화 비용: ~300-600ns/message
핸들링 비용: ~5ns (atomic counter)

동기화/핸들링 비율: 60~120배
```

### How to improve scaling

스케일링을 개선하려면:
1. **핸들러를 무겁게**: I/O, DB, 계산 등 → 동기화 비용 상쇄
2. **메일박스 분리**: 액터별 독립 메일박스 → 뮤텍스 경쟁 감소
3. **Lock-free 구조**: 뮤텍스 대신 CAS 기반 큐 사용
4. **배치 최적화**: maxBatch 조정으로 디스패치 빈도 감소

## 결론

### 핵심 발견

1. **워커 스케일링은 경량 핸들러에서 실패**
   - 1→8 workers에서 throughput 95~100% 감소
   - 세마포어/뮤텍스 경쟁이 병렬화 이득을 압도

2. **액터 스케일링은 단일 워커에서만 작동**
   - workers=1: 1→8 액터에서 throughput 유지 (~900K)
   - workers=4~8: 1→8 액터에서 70~90% 감소

3. **싱글 워커가 최적**
   - 모든 조합에서 1 worker가 최고 성능 (~900K msgs/sec)
   - 동기화 비용이 핸들링 비용의 60~120배

4. **스케일링 효율이 1.0x인 경우는 없음**
   - 최고 효율: workers=1→2에서 0.51x (이론적 2.0x의 25%)
   - 최저 효율: workers=1→8에서 0.00x (이론적 8.0x의 0%)

5. **이 결과는 오버헤드의 기준선(baseline)을 제공**
   - 실제 애플리케이션(파일 I/O, DB 등)에서는 스케일링이 작동할 것으로 예상
   - 핸들러 비용이 동기화 비용과 비슷해지면 병렬화 이득 발생

### 권장 사항

1. **워커 수 선택**:
   - 경량 핸들러: workers=1~2 (최고 성능)
   - 무거운 핸들러: CPU 코어 수만큼 workers 설정
   - **핸들러 비용을 먼저 측정하고 워커 수 결정**

2. **액터 수 선택**:
   - workers = 1이면 액터 수 자유롭게 설정 가능
   - workers > 1이면 액터 수를 줄이는 것이 유리
   - **단일 액터로 충분하면 1개 사용**

3. **시스템 설계 시**:
   - **핸들러 무게와 워커 수의 트레이드오프**를 고려
   - 경량 핸들러에서는 싱글 워커 + 여러 액터가 최적
   - 무거운 핸들러에서는 여러 워커 + 여러 액터가 최적
