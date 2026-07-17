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
Throughput: 920K → 820K → 136K → 42K (역감소!)
Efficiency: 1.0x → 0.45x → 0.07x → 0.02x
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
baseThroughput = throughput(1 worker)
efficiency(N) = throughput(N) / (N × baseThroughput)
```

### 왜 스케일링이 안 되는가? (경량 작업의 경우)

핸들러가 `atomic counter 증가`만 하는 경우:

```
핸들링 비용:  ~5ns  (원자적 증가)
동기화 비용: ~500ns (뮤프스 + 세마포어 + 컨텍스트 스위칭)
```

동기화 비용이 핸들링 비용의 **100배**. 따라서:
- 워커가 늘어나면 동기화 비용이 **기하급수적으로** 증가
- 병렬화의 이득보다 동기화의 비용이 큼

## 실험 결과

### 실험 1: Worker Scaling (actors=4 고정)

| Workers | Throughput (msgs/sec) | Efficiency |
|---------|----------------------|------------|
| 1 | 921,082 | 1.00x |
| 2 | 721,058 | 0.39x |
| 4 | 136,158 | 0.04x |
| 8 | 42,057 | 0.01x |

**분석:**
- 1 worker가 가장 빠름 → **동기화 오버헤드가 병렬화 이득을 압도**
- 2 workers: -11% (세마포어 경쟁 시작)
- 4 workers: -85% (본격적 뮤프스 + 세마포어 경쟁)
- 8 workers: -95% (심각한 스레드 경쟁)

### 실험 2: Actor Scaling (workers=4 고정)

| Actors | Throughput (msgs/sec) | Efficiency |
|--------|----------------------|------------|
| 1 | 120,100 | 1.00x |
| 2 | 57,562 | 0.24x |
| 4 | 31,702 | 0.07x |
| 8 | 41,054 | 0.04x |

**분석:**
- 1 actor가 가장 빠름 → 메일박스가 분산되면 dispatch 비용 증가
- 2 actors: -52% (dispatch가 2개 메일박스를 관리)
- 8 actors: 약간 회복 (메일박스당 메시지 수 감소 → 뮤프스 경쟁 감소)

### 스케일링 곡선 시각화

```
Throughput
  ↑
  │ ★ 921K (1 worker)
  │
  │   ★ 721K (2 workers)
  │
  │
  │
  │         ★ 136K (4 workers)
  │
  │                    ★ 42K (8 workers)
  └──────────────────────────────────────→ Workers
      1     2     4     8
```

## 출력 예시

```
=== scaling ===
Measure worker and actor scaling efficiency

[Config]
  Workers:    16
  Actors:     16
  MaxBatch:   32
  Mailbox:    0
  Iterations: 100,000
  Warmup:     0

  Throughput: 931,973.24 msgs/sec
  Total Time: 107.30 ms

[Worker Scaling]
 Workers      Throughput  Efficiency
------------------------------------
       1    931,973 m/s       1.00x
       2    867,499 m/s       0.47x
       4    379,044 m/s       0.10x
       8    178,017 m/s       0.02x

[Actor Scaling]
  Actors      Throughput  Efficiency
------------------------------------
       1    305,342 m/s       1.00x
       2    130,419 m/s       0.21x
       4     77,072 m/s       0.06x
       8     28,716 m/s       0.01x
```

## 기술적 세부사항

### Why single worker is fastest

단일 워커의 장점:
- 세마포어 경쟁 없음
- 뮤프스 contention 없음
- 캐시 locality 극대화
- 컨텍스트 스위칭 없음

### How to improve scaling

스케일링을 개선하려면:
1. **핸들러를 무겁게**: I/O, DB, 계산 등 → 동기화 비용 상쇄
2. **메일박스 분리**: 액터별 독립 메일박스 → 뮤프스 경쟁 감소
3. **Lock-free 구조**: 뮤프스 대신 CAS 기반 큐 사용
4. **배치 최적화**: maxBatch 조정으로 디스패치 빈도 감소

## 결론

- 경량 핸들러에서는 **싱글 워커가 최적** → 동기화 비용이 병렬화 이득 초과
- 이 결과는 **오버헤드의 기준선(baseline)**을 제공
- 실제 애플리케이션(파일 I/O, DB 등)에서는 스케일링이 작동할 것으로 예상
- 시스템 설계 시 **핸들러 무게와 워커 수의 트레이드오프**를 고려해야 함
