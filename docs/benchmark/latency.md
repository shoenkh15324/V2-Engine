# Latency Benchmark

## 목적

**"메시지 하나가 들어오고 실제로 처리될 때까지 얼마나 걸리는가?"**

개별 메시지의 **끝단 레이턴시(end-to-end latency)**를 측정합니다. 실시간 시스템에서 가장 중요한 지표 중 하나입니다.

## 설계 원리

### 레이턴시가 중요한 이유

 Throughput이 "얼마나 빨리 처리하는가"라면, Latency는 **"메시지 하나가 도착한 후 실제로 처리될 때까지의 대기 시간"**입니다.

 ```
 시점 A: 메시지가 Mailbox에 push됨
 시점 B: Actor::handle()이 실행됨
 레이턴시 = B - A
 ```

- 게임 서버: 플레이어 입력 → 응답까지의 시간
- 실시간 채팅: 메시지 전송 → 수신까지의 시간
- IoT: 센서 데이터 → 처리까지의 시간

### 레이턴시와 처리량의 관계

```
Throughput = 전체 메시지 수 / 전체 시간
Latency = 개별 메시지 대기 시간
```

**동시에 최적화하기 어렵습니다.** 배치 처리(batch processing)는 throughput을 높이지만 latency의 꼬리(tail)를 길게 만듭니다.

### 측정 방법

```
for each message:
  sendTimestamp = now()
  push(message)                    ← 시점 A
  while not handled: spin-wait     ← 메시지가 실제로 처리될 때까지 대기
  receiveTimestamp = now()         ← 시점 B
  latency[i] = receiveTimestamp - sendTimestamp
```

**동기식 측정**: 메시지가 실제로 처리될 때까지 대기한 후 다음 메시지를 보냅니다. 따라서 각 메시지의 정확한 레이턴시를 측정할 수 있습니다.

### 레이턴시 분포

단순 평균만으로는 불완전합니다. **퍼센타일(percentile)** 분포가 필요합니다:

| 퍼센타일 | 의미 | 예시 |
|---------|------|------|
| **P50** | 절반의 메시지가 이 시간 안에 처리됨 | "보통 이 정도 걸림" |
| **P95** | 95%의 메시지가 이 시간 안에 처리됨 | "거의 대부분 이 정도" |
| **P99** | 99%의 메시지가 이 시간 안에 처리됨 | "100번 중 1번은 이 정도" |
| **P999** | 99.9%의 메시지가 이 시간 안에 처리됨 | "극단적 케이스" |
| **Max** | 가장 느린 메시지 | "최악의 경우" |

## 실험 결과

### 실험 1: 기본 설정

```
Config: workers=4, actors=1, iterations=50,000, maxbatch=32

Throughput:  ~36,000 msgs/sec
Avg Latency: 6,720 ns (6.7μs)

Latency Distribution:
  Min:  73 ns
  P50:  6,219 ns (6.2μs)
  P95:  15,291 ns (15.3μs)
  P99:  28,350 ns (28.4μs)
  P999: 43,454 ns (43.5μs)
  Max:  321,725 ns (321.7μs)
```

### 결과 분석

**P50 = 6.2μs**: 메시지의 절반은 6.2μs 안에 처리됩니다.
- `enqueue()` → 뮤텍스 잠금/해제 ≈ 50ns
- `dispatch()` → 세마포어 시그널 ≈ 100ns
- `dequeue()` → 워커가 배치로 꺼냄 → `handle()` 실행
- 이 모든 것이 6.2μs 안에 완료됨

**P99 = 28.4μs**: 100번 중 1번은 28.4μs가 걸립니다.
- 배치 처리(batch processing) 때문
- 어떤 메시지가 32개 배치의 **맨 뒤**에 들어가면 앞의 31개가 처리되는 동안 대기
- `maxBatch=32`이므로 최대 32개 메시지가 동시에 워커에 의해 꺼내짐

**Max = 321.7μs**: 가장 느린 메시지는 321.7μs
- 워커 스케줄링 지연, 컨텍스트 스위칭, 또는 시스템 부하 발생 시

### 결과 해석 가이드

| 레이턴시 | 용도 적합성 |
|---------|-----------|
| P50 < 10μs | 실시간 오디오/비디오, 고빈도 트레이딩 |
| P50 < 100μs | 게임 서버, 실시간 채팅 |
| P50 < 1ms | 일반적인 웹 서비스 |
| P99 > 10ms | 실시간 앱에서는 부적합 |

현재 시스템의 **P50=6.2μs, P99=28.4μs**는 실시간 시스템에 충분한 수준입니다.

## 출력 예시

```
=== latency ===
Measure per-message end-to-end latency

[Config]
  Workers:    4
  Actors:     1
  MaxBatch:   32
  Mailbox:    50,256
  Iterations: 50,000
  Warmup:     0

  Throughput: 36,246.69 msgs/sec
  Avg Latency: 6,720.33 ns
  Total Time: 1,379.44 ms

[Latency Distribution]
  Min:  73.00 ns
  P50:  6,219.00 ns
  P95:  15,291.00 ns
  P99:  28,350.00 ns
  P999: 43,454.00 ns
  Max:  321,725.00 ns

[Actors]
  latency_actor  mailbox=50256  processed=50000
```

## 기술적 세부사항

### Why P99 is much higher than P50

배치 처리의 기작:

```
Mailbox: [M1][M2][M3]...[M32] (32개 메시지)
                ↓
Worker:  M1, M2, M3, ..., M32를 한 번에 꺼내 처리
```

- M1이 들어올 때: 메일박스가 비어있음 → 즉시 처리 (73ns)
- M16이 들어올 때: 앞에 15개가 이미 있음 → 대기 후 처리
- M32이 들어올 때: 31개가 이미 있음 → 가장 오래 대기

이 **"배치의 위치"**에 따라 레이턴시가 달라집니다.

## 결론

- P50=6.2μs, P99=28.4μs → 실시간 시스템에 적합한 레이턴시
- maxBatch를 줄이면 P99가 개선되지만 throughput이 하락하는 **트레이드오프** 존재
- Tail latency(P99, P999)는 시스템 안정성에 중요한 지표
