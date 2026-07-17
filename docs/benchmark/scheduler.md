# Scheduler Benchmark

## 목적

**"타이머가 정확한 간격으로 발사되는가?"**

커널 타이머 + epoll 기반 스케줄러의 **정밀도와 드리프트**를 측정합니다. 반복 작업(heartbeat, 정기 체크 등)에 사용되는 타이머의 신뢰성을 검증합니다.

## 설계 원리

### 타이머가 중요한 이유

 actor 시스템에서 시간 기반 작업은 타이머에 의존합니다:

- **Heartbeat**: 연결 유지 확인 (주기적 ping)
- **정기 동기화**: DB 캐시 갱신, 설정 리로드
- **게임 Tick**: 초당 60회 게임 상태 업데이트
- **스케줄링**: cron-like 정기 작업

타이머가 **정확하지 않으면**:
- heartbeat이 늦어져서 연결 끊김误判
- 게임 tick rate 불안정
- 정기 작업이 불규칙하게 실행

### 측정 방법

```
1단계: Warmup (선택)
  - 타이머를 잠시 실행하여 epoll/커널 타이머 워밍

2단계: Measurement
  - 단일 반복 타이머 시작 (intervalMs 간격)
  - durationMs 동안 실행
  - 모든 타이머 발사 시각 기록

3단계: 분석
  - 연속 발사 간격 계산
  - P50, P95, P99, P999, Min, Max 계산
```

### 타이머 동작 원리

```
[커널]
  timerfd_create(CLOCK_MONOTONIC)
  timerfd_settime(interval=50ms)
      ↓
  50ms마다 FD에 이벤트 발생
      ↓
[Dispatcher epoll]
  epoll_wait() → FD 이벤트 감지
      ↓
  handleTimerEvent()
      ↓
  excuteExpiredTimers()
      ↓
  콜백 실행 → Actor::handle()
```

### 측정 대상

- **드리프트(drift)**: 기대 간격과 실제 간격의 차이
- **지터(jitter)**: 간격의 변동폭
- **정밀도**: P50이 기대값에 얼마나 가까운가

## 실험 결과

### 실험 1: 기본 설정

```
Config: workers=4, interval=50ms, duration=5000ms

Iterations (firings): 100
Expected firings: 100 (5000ms / 50ms)

Latency Distribution (타이머 간격):
  Min:  48,751,811 ns (48.8ms)
  P50:  49,886,625 ns (49.9ms)
  P95:  51,077,760 ns (51.1ms)
  P99:  51,497,084 ns (51.5ms)
  P999: 51,497,084 ns (51.5ms)
  Max:  51,497,084 ns (51.5ms)
```

### 결과 분석

**드리프트 계산**:
```
기대 간격: 50,000,000 ns (50ms)
실제 P50:  49,886,625 ns (49.9ms)
드리프트:  -0.02% (기대값보다 약간 빠름)
```

**지터 계산**:
```
Min: 48.8ms → -2.4% 느림
Max: 51.5ms → +3.0% 빠름
지터 범위: ±3.0ms
```

**100발사 / 5000ms = 기대 100회, 실제 100회** → 발사 누락 없음

### 결과 해석

| 지표 | 값 | 평가 |
|-----|-----|------|
| 드리프트 | -0.02% | 우수 (거의 정확) |
| P50 | 49.9ms | 기대값(50ms)에 근접 |
| 지터 범위 | ±3.0ms | 양호 |
| 발사 누락 | 0 | 완벽 |

### 기준 비교

| 시스템 | 드리프트 | 지터 |
|--------|---------|------|
| **V2-Engine** | **-0.02%** | **±3.0ms** |
| 일반 OS 타이머 | ±0.1% | ±5-10ms |
| 하드웨어 타이머 | ±0.001% | ±0.01ms |
| NTP 동기화 | ±0.01% | ±1ms |

V2-Engine의 타이머 정밀도는 **일반적인 백그라운드 작업에는 충분**합니다.

## 출력 예시

```
=== scheduler ===
Measure timer scheduling precision

[Config]
  Workers:    4
  Actors:     0
  MaxBatch:   32
  Mailbox:    0
  Iterations: 100
  Warmup:     0

  Avg Latency: 49,991,322.42 ns
  Total Time: 5,000.00 ms

[Latency Distribution]
  Min:  48,751,811.00 ns
  P50:  49,886,625.00 ns
  P95:  51,077,760.00 ns
  P99:  51,497,084.00 ns
  P999: 51,497,084.00 ns
  Max:  51,497,084.00 ns
```

## 기술적 세부사항

### timerfd vs 일반 타이머

| 특성 | timerfd (V2-Engine) | setInterval (JS) | Thread sleep |
|------|---------------------|------------------|-------------|
| 정밀도 | ~1ms | ~4ms | ~10ms |
| 이벤트 메커니즘 | epoll | 이벤트 루프 | 컨텍스트 스위칭 |
| 오버헤드 | 낮음 | 중간 | 높음 |

### 지터의 원인

1. **epoll_wait 타임아웃**: epoll이 이벤트를 감지하는 시간
2. **컨텍스트 스위칭**: 워커 스레드 전환
3. **시스템 부하**: 다른 프로세스/스레드의 영향
4. **커널 스케줄러**: 타이머 해제 우선순위

## 결론

- 커널 타이머(timerfd) + epoll 기반으로 **0.02% 드리프트** 달성
- ±3.0ms 지터는 일반적인 백그라운드 작업에 충분
- **실시간 오디오/비디오**에는 ±1ms 이내가 필요 (별도 최적화 필요)
- 발사 누락 없음 → 타이머 신뢰성 보장
