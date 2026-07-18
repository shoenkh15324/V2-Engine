# Mailbox Selection: Mutex vs LockFree

V2-Engine의 메일박스 구현체 선택에 대한 아키텍처 결정 문서입니다.

## Decision

**LockFreeMailbox를 기본값으로 선택**, MutexMailbox는 대체재로 유지.

## Background

V2-Engine은 두 가지 메일박스 구현체를 제공합니다:

| 구현체 | 동기화 메커니즘 | 특성 |
|--------|----------------|------|
| `MutexMailbox<T>` | `std::mutex` | 범용, MPMC 가능 |
| `LockFreeMailbox<T>` | CAS (Compare-And-Swap) | MPSC 전용, 락프리 |

## Performance Comparison

### Throughput (시스템 레벨)

| Workers | MutexMailbox | LockFreeMailbox | LF/Mutex |
|---------|-------------|-----------------|----------|
| 1 | 866K msgs/sec | **5.5M msgs/sec** | **6.4x** |
| 2 | 862K msgs/sec | **7.1M msgs/sec** | **8.3x** |
| 4 | 443K msgs/sec | 107K msgs/sec | 0.24x |
| 8 | 207K msgs/sec | 10K msgs/sec | 0.05x |

### Latency

| Workers | Mutex (P50/P99) | LockFree (P50/P99) |
|---------|-----------------|---------------------|
| 1 | **357 ns** / 881 ns | 378 ns / **641 ns** |
| 2 | **390 ns** / 12.3μs | 440 ns / 17.2μs |
| 4 | **469 ns** / **18.5μs** | 10.9μs / 21.9μs |

### Contention (동시 push)

| Producers | MutexMailbox | LockFreeMailbox | LF/Mutex |
|-----------|-------------|-----------------|----------|
| 2 | 907K msgs/sec | **7.97M msgs/sec** | **8.8x** |
| 8 | 850K msgs/sec | **6.16M msgs/sec** | **7.3x** |
| 32 | 890K msgs/sec | **5.22M msgs/sec** | **5.9x** |

### Backpressure

| maxBatch | Mutex Drop Rate | LockFree Drop Rate |
|----------|----------------|-------------------|
| 1 | 85.58% | 85.52% |
| 8 | 42.98% | 41.45% |
| 32 | **0.00%** | **0.00%** |

→ 백프레셔는 mailbox 타입과 **무관** (드롭 로직이 동일)

## Tradeoffs

| 조건 | MutexMailbox | LockFreeMailbox |
|------|-------------|-----------------|
| workers=1~2 | | ✅ **6~8x faster** |
| workers=4+ | ✅ **더 빠름** | ❌ MPSC 제약 |
| 동시 push | | ✅ **5~9x faster** |
| 동시 pop (MPMC) | ✅ **가능** | ❌ 불가 |
| 백프레셔 | 동일 | 동일 |

## Rationale

### Default: LockFreeMailbox

1. **싱글/듀얼 워커에서 결정적 이점** — 6~8x throughput 향상
2. **동시 push 시나리오에서 압도적** — 8.8x throughput 향상
3. **현실적 워커 수**: 대부분의 시스템이 1~2개 워커로 충분
4. **P99 tail latency 개선**: 641ns vs 881ns (workers=1)

### When to Use MutexMailbox

1. **멀티 컨슈머 시나리오** — MPSC 제약 회피 필요
2. **workers=4+ 환경** — MPSC + readyQueue 병목
3. **범용성 필요** — MPMC 보장 필요

## Recommendation

```cpp
// 기본값: LockFreeMailbox
auto* actor = system.createActor<MyActor>("actor1", 1024);

// MutexMailbox가 필요할 때만
auto* actor = system.createActor<MyActor>("actor2", 1024, MailboxType::Mutex);
```

## See Also

- [Throughput Benchmark](../benchmark/throughput.md)
- [Latency Benchmark](../benchmark/latency.md)
- [Contention Benchmark](../benchmark/contention.md)
