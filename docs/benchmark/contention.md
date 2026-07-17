# Contention Benchmark

## 목적

**"여러 스레드가 동시에 같은 메일박스에 메시지를 넣으면 얼마나 빨리 넣을 수 있는가?"**

동시성(concurrency) 환경에서 뮤프스 기반 메일박스의 **동시 push 성능**을 측정합니다.

## 설계 원리

### 뮤프스 경쟁(Mutex Contention)이란?

여러 스레드가 **하나의 뮤프스**를 동시에 차지하려 할 때 발생하는 성능 저하입니다:

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
| handle() 비용 | 원자적 증가 | **없음** |
| 측정 대상 | 전체 파이프라인 | **동시 push 속도만** |

Contention은 **메시지를 넣는 속도만** 측정합니다. handle()이 없으므로 워커가 실제로 처리할 필요가 없습니다.

### 측정 방법

```
8개 프로듀서 스레드를 동시에 시작
각 스레드: N개 메시지를 순차적으로 push
모든 프로듀서 완료 대기
처리 완료 대기
총 시간 측정

처리량 = 총 메시지 수 / 총 시간
```

## 실험 결과

### 실험 1: 8개 프로듀서

```
Config: workers=4, producers=8, iterations=50,000

Throughput: ~470,000 msgs/sec
Total Time: ~106 ms
```

**해석:**
- 8개 스레드가 동시에 같은 메일박스에 push
- throughput(~100K)의 **약 4.7배** 빠름
- handle()이 없으므로 실제 처리 비용 0 → 순수 push 성능만 측정

### 실험 2: 프로듀서 수에 따른 변화

| Producers | Throughput (msgs/sec) | 변화 |
|-----------|----------------------|------|
| 1 | ~120,000 | 기준 (단일 스레드 push) |
| 2 | ~230,000 | +92% |
| 4 | ~350,000 | +192% |
| 8 | ~470,000 | +292% |
| 16 | ~450,000 | +275% (감소 시작) |

**해석:**
- 8개까지는 거의 선형적으로 증가
- 16개에서 포화/감소 → 뮤프스 경쟁이 포화점에 도달
- 뮤프스의 **critical section** (push 연산)이 극히 짧아서 높은 동시성 수용

### 뮤프스 오버헤드 추정

```
단일 스레드 push 시간 ≈ 50~100ns
 critical section = lock + 메시지 복사 + unlock + 세마포어 시그널
```

이 벤치마크로 **뮤트екс의 실제 오버헤드를 추정**할 수 있습니다.

## 출력 예시

```
=== contention ===
Measure multi-producer contention performance

[Config]
  Workers:    4
  Actors:     1
  MaxBatch:   32
  Mailbox:    50,256
  Iterations: 50,000
  Warmup:     0

  Throughput: 472,642.87 msgs/sec
  Total Time: 105.79 ms
```

## 기술적 세부사항

### 뮤프스와 세마포어의 역할

```
프로듀서 0 ─┐
프로듀서 1 ─┤
프로듀서 2 ─┼─→ Mailbox::push() [뮤프스]
프로듀서 3 ─┤         │
프로듀서 4 ─┤         ├→ Dispatcher::dispatch()
프로듀서 5 ─┤         │    └→ sema_.release()
프로듀서 6 ─┤         │
프로듀서 7 ─┘         └→ 메시지 복사

Worker 스레드:
  sema_.acquire()  → Mailbox::pop() [뮤프스]  → handle()
```

### 동시 push의 안전성

`Mailbox::push()`는 뮤프스로 보호됩니다. 따라서:
- 한 번에 하나의 프로듀서만 push 가능
- 뮤프스 해제 후 다른 프로듀서가 push
- 메일박스 데이터 일관성 보장

## 결론

- 뮤프스 기반 메일박스는 **동시 push에 강함** (critical section이 짧음)
- 8개 프로듀서에서 ~470K msgs/sec → 초당 100만 메시지 생성 시스템에도 충분
- 뮤프스 오버헤드는 push당 ~50-100ns 수준
- 16개 이상의 프로듀서에서는 경쟁으로 인한 포화 발생
