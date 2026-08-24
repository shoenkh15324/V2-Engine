# Concurrency

---

## Table of Contents

- [Overview](#overview)
- [Design Principles](#design-principles)
- [Lock-Free Data Structures](#lock-free-data-structures)
  - [MPSC Queue](#mpsc-queue-multi-producer-single-consumer)
  - [MPMC Queue](#mpmc-queue-multi-producer-multi-consumer)
  - [Cache Line Awareness](#cache-line-awareness)
- [Threading Model](#threading-model)
  - [Worker Threads](#worker-threads)
  - [Semaphore-Based Wake-Up](#semaphore-based-wake-up)
  - [Mutex Usage Boundaries](#mutex-usage-boundaries)
- [Work Distribution](#work-distribution)
  - [Actor Affinity](#actor-affinity)
  - [Load-Aware Dispatch](#load-aware-dispatch)
  - [Work Stealing](#work-stealing)
  - [Adaptive Backoff](#adaptive-backoff)
  - [Drain Protocol](#drain-protocol)
- [Actor Thread Safety](#actor-thread-safety)
  - [Single-Execution Guarantee](#single-execution-guarantee)
  - [Scheduled Flag (Deduplication Gate)](#scheduled-flag-deduplication-gate)
  - [Restart Count CAS Loop](#restart-count-cas-loop)
  - [Stopped Flag](#stopped-flag)
- [Memory Ordering Reference](#memory-ordering-reference)
- [Event Loop Integration](#event-loop-integration)
  - [Cross-Thread Posting](#cross-thread-posting)
  - [Thread Affinity Awareness](#thread-affinity-awareness)
- [Thread-Local Storage](#thread-local-storage)
  - [TCMalloc-Style Memory Cache](#tcmalloc-style-memory-cache)
  - [Worker-Confined Backoff State](#worker-confined-backoff-state)
- [Backpressure & Flow Control](#backpressure--flow-control)
- [Summary](#summary)

---

## Overview

> 📌 **2026-08-24**: 실행 토큰 생명주기·inFlight 슬롯·finalize 정산·스핀 티어 설계는
> [Scheduling](scheduling.md)으로 이관되었습니다. 이 문서의 일부 절(Worker 루프의 `onWorkDone`,
> Scheduled Flag 게이트, 메모리 오더 표의 `scheduled_` 행)은 구 설계 기록으로 남아 있습니다.

V² Engine uses an actor-based concurrency model built on three pillars: **lock-free data structures**, a **work-stealing thread pool**, and **actor affinity**. Thread safety is achieved through message passing via lock-free queues rather than shared-state locking. Locks are confined to infrastructure components that sit outside the message hot path.

---

## Design Principles

| Principle | Description |
|-----------|-------------|
| **Lock-free hot path** | Message enqueue, dispatch, acquire, and batch processing use only atomics and lock-free queues. No mutex on the critical path. |
| **Single-execution guarantee** | An actor's `handle()` is called by exactly one worker at a time. No actor-level locking needed. |
| **Cooperative scheduling** | Workers process messages in configurable batches (default: 32) then yield, ensuring fairness. |
| **Lock boundaries** | Mutexes are restricted to infrequent paths (timer registration, registry writes, fallback queues) and infrastructure (memory allocator, logger). |
| **Zero-copy message passing** | Messages are moved (not copied) through lock-free queues. SBO optimization avoids heap allocation for small messages. |

---

## Lock-Free Data Structures

### MPSC Queue (Multi-Producer, Single-Consumer)

**File:** `src/core/common/container/lock_free_mpsc_queue.hpp`

Dmitry Vyukov's sequence-lock ring buffer. Used for actor mailboxes, dead letter queues, and cross-thread event posting.

**Slot layout:**

```
Slot {
    std::atomic<size_t> sequence;   // ownership token
    alignas(T) std::byte storage[sizeof(T)];  // inline element storage
}
```

**Push (any thread):**

```cpp
bool push(T&& msg) noexcept {
    size_t pos = tail_.value.load(std::memory_order_relaxed);
    for(;;){
        Slot& slot = slots_[pos % capacity_];
        size_t seq = slot.sequence.load(std::memory_order_acquire);  // (1)
        auto diff = static_cast<SignedSize>(seq) - static_cast<SignedSize>(pos);
        if(diff == 0){
            if(tail_.value.compare_exchange_weak(pos, pos + 1,       // (2)
                std::memory_order_relaxed, std::memory_order_relaxed)){
                ::new (static_cast<void*>(slot.storage)) T(std::move(msg));
                slot.sequence.store(pos + 1, std::memory_order_release);  // (3)
                return true;
            }
        }else if(diff < 0){
            return false;  // queue full
        }else{
            pos = tail_.value.load(std::memory_order_relaxed);  // retry
        }
    }
}
```

| Step | Operation | Memory Ordering | Rationale |
|------|-----------|-----------------|-----------|
| (1) | Read `slot.sequence` | `acquire` | See the latest state of this slot |
| (2) | CAS on `tail_` | `relaxed` | Contention resolution only; no data dependency |
| (3) | Write `slot.sequence` | `release` | Publish placement-new to the consumer |

**Pop (single consumer only):**

```cpp
bool pop(T& out) noexcept {
    size_t pos = head_.value.load(std::memory_order_relaxed);
    Slot& slot = slots_[pos % capacity_];
    size_t seq = slot.sequence.load(std::memory_order_acquire);  // (4)
    auto diff = static_cast<SignedSize>(seq) - static_cast<SignedSize>(pos + 1);
    if(diff != 0) return false;

    T* element = slot.element();
    out = std::move(*element);
    element->~T();

    head_.value.store(pos + 1, std::memory_order_relaxed);       // (5)
    slot.sequence.store(pos + capacity_, std::memory_order_release);  // (6)
    return true;
}
```

| Step | Operation | Memory Ordering | Rationale |
|------|-----------|-----------------|-----------|
| (4) | Read `slot.sequence` | `acquire` | Read the published element |
| (5) | Write `head_` | `relaxed` | Safe — single consumer, no data dependency |
| (6) | Write `slot.sequence` | `release` | Recycle slot for producers |

**Used by:**
- `Mailbox` — every actor's mailbox
- `DeadLetterQueue` — failed messages
- `EventLoopEpoll::pendingOps_` — cross-thread event posting

---

### MPMC Queue (Multi-Producer, Multi-Consumer)

**File:** `src/core/common/container/lock_free_mpmc_queue.hpp`

Dmitry Vyukov's bounded MPMC queue. Used for `WorkDispatcher` per-worker ready queues (work stealing requires multiple consumers).

**Key difference from MPSC:** `pop()` uses CAS on `head_` because multiple workers may dequeue concurrently from the same queue.

```cpp
bool pop(T& out) noexcept {
    size_t pos = head_.value.load(std::memory_order_relaxed);
    for(;;){
        Slot& slot = slots_[pos % capacity_];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        auto diff = static_cast<SignedSize>(seq) - static_cast<SignedSize>(pos + 1);
        if(diff == 0){
            if(head_.value.compare_exchange_weak(pos, pos + 1,     // CAS (not just store)
                std::memory_order_relaxed)){
                T* element = slot.element();
                out = std::move(*element);
                element->~T();
                slot.sequence.store(pos + capacity_, std::memory_order_release);
                return true;
            }
        }else if(diff < 0){
            return false;
        }else{
            pos = head_.value.load(std::memory_order_relaxed);
        }
    }
}
```

**Used by:** `WorkDispatcher` — one per worker, enabling cross-worker stealing.

---

### Cache Line Awareness

**File:** `src/core/common/container/cache_line.hpp`

```cpp
#if defined(__APPLE__) && defined(__arm64__)
    inline constexpr size_t kCacheLine = 128;
#elif defined(__aarch64__)
    inline constexpr size_t kCacheLine = 64;
#else
    inline constexpr size_t kCacheLine = 64;
#endif
```

All queue `head_`/`tail_` atomics and performance metric counters are aligned to `kCacheLine` to prevent **false sharing** between adjacent data structures on the same cache line.

```cpp
struct alignas(kCacheLine) AlignedAtomic {
    std::atomic<size_t> value{0};
};
```

---

## Threading Model

### Worker Threads

**File:** `src/core/actor_system/runtime/dispatcher/worker.hpp`, `worker.cpp`

Each `Worker` owns a `std::thread` and an `std::atomic<bool> running_`.

**Worker loop:**

```
┌─────────────────────────────────────────────┐
│ while(running_ || draining_)                 │
│   idleStartTime = now()                      │
│   actorRuntime = dispatcher->acquire(id)    │  ← blocks on semaphore / steals
│   idleEndTime = now()                        │
│                                              │
│   if(!actorRuntime):                         │
│     drainPendedActor()                       │  ← retry fallback queue
│     if draining && pendingWork==0: break     │
│     continue                                 │
│                                              │
│   busyStartTime = now()                      │
│   processed = actorRuntime->run(maxBatch)    │  ← batch of messages
│   busyEndTime = now()                        │
│   if(!more) dispatcher->onWorkDone()         │  ← decrement pendingWork
│   recordMetrics(...)                         │
└─────────────────────────────────────────────┘
```

Key properties:
- Workers block on `std::counting_semaphore` when idle — efficient OS-level sleep, no busy-wait
- Batch processing (`maxBatch=32`) amortizes dispatch overhead
- Cooperative scheduling — actors yield after their batch
- Metrics track idle/busy time per worker

---

### Semaphore-Based Wake-Up

**File:** `src/core/actor_system/runtime/dispatcher/work_dispatcher.hpp:61`

`WorkDispatcher` holds one `std::counting_semaphore<>` per worker:

| Operation | Where | Effect |
|-----------|-------|--------|
| `semas_[workerId]->release()` | `enqueueEntry()` (line 111) | Wake the target worker when new work is dispatched |
| `semas_[workerId]->try_acquire_for()` | `acquire()` (line 66) | Worker sleeps with timeout, wakes on signal or timeout |
| `semas_[victim]->try_acquire()` | `trySteal()` (line 146) | Steal the semaphore token when popping from another worker's queue |
| `semas_[i]->release()` (all) | `beginDrain()`, `onWorkDone()` | Wake all workers for shutdown |

---

### Mutex Usage Boundaries

Locks are confined to **infrequent** or **fallback** paths:

| Mutex | Location | Purpose | Hot Path? |
|-------|----------|---------|-----------|
| `WorkDispatcher::mutex_` | `work_dispatcher.hpp:55` | Guards `pendingActorList_` fallback queue | No — only when lock-free push fails |
| `ActorRuntime::timerMutex_` | `actor_runtime.hpp:67` | Guards `timerIds_` set | No — timer add/cancel is infrequent |
| `Scheduler::mutex_` | `scheduler.hpp:33` | Guards timer registration map | No — timer operations |
| `Supervisor::mutex_` | `supervisor.hpp:62` | Guards per-actor strategy overrides | No — policy lookup |
| `CentralCache::mutex_` | `central_cache.hpp:115` | Guards slab allocation/deallocation | Infrequent — batched refills |
| `ActorRegistry::mutex_` | `actor_registry.hpp:38` | `shared_mutex` for actor lookup/add/remove | Reads: shared; Writes: unique |
| `Logger::mutex_` | `log.cpp:34` | Guards file I/O for log output | No — batched writes |
| `EventLoopEpoll::handlersMutex_` | `event_loop_epoll.hpp:36` | Guards fd-to-handler map | No — subscribe/unsubscribe only |

**The message hot path (enqueue → dispatch → acquire → run → handle) is entirely lock-free.**

---

## Work Distribution

### Actor Affinity

Each actor is deterministically assigned to a "home" worker:

```cpp
int WorkDispatcher::pickWorker(uint64_t actorId) {
    int home = static_cast<int>(actorId % workerCount_);
    if(workerCount_ <= 1) return home;
    if(queues_[home]->count() < static_cast<size_t>(highWatermark_)) return home;
    return pickLeastLoaded(actorId);
}
```

This ensures:
- **Cache locality** — the same actor's messages are usually processed by the same worker (warm L1/L2)
- **Minimal contention** — only the home worker pops from the actor's MPSC mailbox; any thread can push

---

### Load-Aware Dispatch

When the home worker's queue exceeds the **high watermark** (70% of queue capacity), the dispatcher routes to the **least-loaded worker**:

```cpp
int WorkDispatcher::pickLeastLoaded(uint64_t actorId) {
    int best = static_cast<int>(actorId % workerCount_);
    size_t bestCount = queues_[best]->count();
    for(int i = 0; i < workerCount_; i++) {
        int w = static_cast<int>((best + i) % workerCount_);
        size_t c = queues_[w]->count();
        if(c < bestCount) {
            best = w;
            bestCount = c;
            if(bestCount == 0) break;
        }
    }
    return best;
}
```

If the chosen queue is full, the `ActorRuntime*` falls back to `pendingActorList_` (mutex-protected) and is retried during worker idle time via `drainPendedActor()`.

---

### Work Stealing

When a worker's own queue is empty and it cannot acquire work via semaphore, it **steals** from neighboring workers:

```cpp
bool WorkDispatcher::trySteal(int workerId, ActorRuntime*& out) {
    for(int i = 1; i < workerCount_; i++) {
        int victim = (workerId + i) % workerCount_;
        if(queues_[victim]->empty()) continue;
        if(queues_[victim]->pop(out)) {
            semas_[victim]->try_acquire();  // consume the semaphore token
            return true;
        }
    }
    return false;
}
```

The steal iterates over all other workers' MPMC queues. Since MPMC queues allow multiple consumers, stealing is safe without additional synchronization.

---

### Adaptive Backoff

Workers transition between two steal intervals based on idle history:

| State | Interval | Behavior |
|-------|----------|----------|
| **Busy** (just found work) | `busyStealIntervalUs` (200 μs default) | Aggressive stealing — high throughput |
| **Idle** (no work found) | `idleStealIntervalUs` (2000 μs default) | Conservative stealing — reduce CPU spin |

```cpp
auto interval = idleBackoff_[workerId] ? idleStealIntervalUs_ : busyStealIntervalUs_;
if(semas_[workerId]->try_acquire_for(std::chrono::microseconds(interval))) {
    // ...
}
if(trySteal(workerId, ctx)) {
    idleBackoff_[workerId] = 0;  // found work → busy
    return ctx;
}
idleBackoff_[workerId] = 1;  // no work → idle
```

`idleBackoff_` is a `std::vector<uint8_t>` indexed by worker ID — each element is only accessed by its owning worker, requiring no synchronization.

---

### Drain Protocol (Graceful Shutdown)

```
1. beginDrain()
   ├── running_ = false
   ├── draining_ = true
   └── release all semaphores → wake all workers

2. Workers finish current batch
   └── onWorkDone() decrements pendingWork_ (acq_rel)

3. When pendingWork_ reaches 0
   └── Last worker releases all semaphores → all workers break out of loop

4. stop()
   ├── Join all worker threads
   └── Clear pending state
```

---

## Actor Thread Safety

### Single-Execution Guarantee

An actor's `handle()` is only ever called from the worker that holds its `ActorRuntime*`. The MPSC mailbox ensures that only one thread (the owning worker) pops messages, while any thread can push.

```
Thread A: enqueue(msg1) → push to mailbox → dispatch to worker 2
Thread B: enqueue(msg2) → push to mailbox → scheduled_ already true, skip dispatch
Worker 2: pop(msg1) → handle(msg1) → pop(msg2) → handle(msg2)
```

No actor-level locking is needed — the queue's single-consumer property provides mutual exclusion.

---

### Scheduled Flag (Deduplication Gate)

> ⚠️ **2026-08-24 갱신**: 이 절은 구 설계(`ActorRuntime::scheduled_`)를 다룹니다. 현재는
> `WorkDispatcher` 내부의 inFlight 슬롯 + `finalize()` 정산 프로토콜로 대체되었습니다 —
> 현행 설계는 [Scheduling](scheduling.md) §4~6을 참고하세요.

**File:** `src/core/actor_system/runtime/actor_runtime/actor_runtime.hpp` *(구현 제거됨 — 역사 문서)*

```cpp
alignas(kCacheLine) std::atomic<bool> scheduled_{false};
```

This is the critical mechanism that ensures only one worker processes a given actor at any time.

**In `enqueue()` (any thread):**

```cpp
if(!scheduled_.exchange(true, std::memory_order_seq_cst)) {
    // We are the first to schedule → dispatch to WorkDispatcher
    workDispatcher_->dispatch(this);
}
// else: already scheduled → skip redundant dispatch
```

**In `run()` (owning worker):**

```cpp
// After processing batch, if not redispatched:
scheduled_.store(false, std::memory_order_seq_cst);
if(!stopped_ && !mailbox_->empty()) {
    if(!scheduled_.exchange(true, std::memory_order_seq_cst)) {
        workDispatcher_->redispatch(this);  // new messages arrived while clearing
    }
}
```

The `seq_cst` ordering provides a full fence, ensuring the check-then-schedule race is safe across threads. This pattern prevents redundant dispatches during concurrent enqueue storms.

---

### Restart Count CAS Loop

**File:** `src/core/actor_system/runtime/actor_runtime/actor_runtime.cpp:105-113`

```cpp
bool ActorRuntime::tryRestart(const std::string& reason, int maxRestarts) {
    int prev = restartCount_.load(std::memory_order_relaxed);
    while(true) {
        if(prev >= maxRestarts) return false;
        if(restartCount_.compare_exchange_weak(prev, prev + 1,
            std::memory_order_relaxed)) break;
    }
    performRestart(reason);
    return true;
}
```

Uses `compare_exchange_weak` in a spin loop to atomically increment the restart count only if below `maxRestarts`. This prevents TOCTOU races between multiple supervisor callbacks. The `relaxed` ordering is sufficient because the CAS itself provides atomicity, and no other data depends on the ordering.

---

### Stopped Flag

**File:** `src/core/actor_system/runtime/actor_runtime/actor_runtime.hpp:70`

```cpp
std::atomic<bool> stopped_{false};
```

Set with `memory_order_relaxed` because it is only checked at the beginning of `processBatch()`. The relaxed ordering is sufficient — the actual state transition (`actor_->close()`) provides its own ordering, and the flag is advisory.

---

## Memory Ordering Reference

| Location | Operation | Ordering | Rationale |
|----------|-----------|----------|-----------|
| MPSC/MPMC `push` | CAS on `tail_` | `relaxed` | Contention resolution only; no data dependency |
| MPSC/MPMC `push` | `slot.sequence` store | `release` | Publishes placement-new to consumer |
| MPSC `pop` | `slot.sequence` load | `acquire` | Reads the published element |
| MPSC `pop` | `head_` store | `relaxed` | Single consumer, no data dependency |
| MPMC `pop` | CAS on `head_` | `relaxed` | Contention resolution among consumers |
| MPMC `pop` | `slot.sequence` load | `acquire` | Reads the published element |
| MPMC `pop` | `slot.sequence` store | `release` | Recycles slot for producers |
| `Worker::running_` | store/load | `release`/`relaxed` | Store fences on transition; load is advisory |
| `WorkDispatcher::running_`, `draining_` | store/load | `release`/`relaxed` | Same pattern |
| `WorkDispatcher::pendingWork_` | `fetch_add` | `relaxed` | Increment is unidirectional |
| `WorkDispatcher::pendingWork_` | `fetch_sub` | `acq_rel` | Synchronize drain with workers — the subtracting thread must see all prior writes |
| `ActorRuntime::scheduled_` | `exchange`, `store` | `seq_cst` | Full fence for the dispatch deduplication gate |
| `ActorRuntime::restartCount_` | `compare_exchange_weak` | `relaxed` | Spin loop; atomicity is sufficient |
| `ActorRuntime::stopped_` | store/load | `relaxed` | Advisory flag; actual state transition provides ordering |
| Metrics `updatePeak` | `compare_exchange_weak` | `relaxed` | Best-effort peak tracking |

---

## Event Loop Integration

### Cross-Thread Posting

**File:** `src/infra/platform/linux/event_loop_epoll.hpp:38`, `event_loop_epoll.cpp:110-118`

The event loop runs on a dedicated thread (`v2-main`). Any thread can post work to it via a lock-free MPSC queue:

```cpp
void EventLoopEpoll::post(std::function<void()> op) {
    while(!pendingOps_.push(std::move(op))) {
        std::this_thread::yield();  // busy-wait if queue full
    }
    // Wake epoll_wait via eventfd
    uint64_t one = 1;
    ::write(stopFd_, &one, sizeof(one));
}

void EventLoopEpoll::drainPendingOps() {
    std::function<void()> op;
    while(pendingOps_.pop(op)) {
        op();
    }
}
```

The event loop drains `pendingOps_` before each `epoll_wait`, ensuring all cross-thread operations are processed promptly. The `eventfd` write wakes the blocking `epoll_wait` immediately.

---

### Thread Affinity Awareness

`subscribe()` and `unsubscribe()` detect whether they are called from the event loop thread:

```cpp
int EventLoopEpoll::subscribe(WatchedFd fd, Handler handler) {
    bool isLoopThread = std::this_thread::get_id() == threadId_;
    {
        std::lock_guard lock(handlersMutex_);
        handlers_[fd] = std::move(handler);
    }
    auto add = [this, fd](){ /* epoll_ctl ADD */ };
    if(isLoopThread) return add();      // direct call
    post([add = std::move(add)]() mutable { add(); });  // deferred
    return Ok;
}
```

If called from the loop thread, `epoll_ctl` is executed directly. Otherwise, the operation is posted to be executed on the loop thread, avoiding the need to lock `epoll_ctl` calls concurrently.

---

## Thread-Local Storage

### TCMalloc-Style Memory Cache

**File:** `src/core/common/memory/thread_local_cache.hpp`

Each thread gets a per-pool array of `ThreadLocalCache` objects — one per size class:

```cpp
inline thread_local std::array<ThreadLocalCache, kMaxPools> poolCaches;
```

**Allocation path (no locks):**

```cpp
void* allocate(std::size_t size) {
    auto& cache = caches_[SizeClass::index(size)];
    if(cache.freeList.count() > 0) return cache.freeList.pop();  // fast path
    return fetchFromCentral(idx);  // slow path: batch refill from CentralCache (mutex)
}
```

**Deallocation path (no locks):**

```cpp
void deallocate(void* ptr, std::size_t size) {
    auto& cache = caches_[SizeClass::index(size)];
    cache.freeList.push(ptr);
    if(cache.freeList.count() > SizeClass::batchSize(idx)) {
        returnToCentral(idx);  // batch return to CentralCache (mutex)
    }
}
```

**Three-tier architecture:**

```
ThreadLocalCache (lock-free FreeList per size class)
    ↓ cache miss (batch refill)
CentralCache (mutex-guarded, per size-class)
    ↓ out of space
Slab (raw memory)
```

The thread-local layer eliminates contention on the hot path. Mutex contention only occurs during batch refills and returns, which happen infrequently.

---

### Worker-Confined Backoff State

**File:** `src/core/actor_system/runtime/dispatcher/work_dispatcher.hpp:59`

```cpp
std::vector<uint8_t> idleBackoff_;  // indexed by worker ID
```

Each element is only read/written by its owning worker — no synchronization needed. Tracks whether a worker has transitioned to idle backoff mode for steal interval selection.

---

## Backpressure & Flow Control

| Mechanism | Behavior |
|-----------|----------|
| **Semaphore blocking** | Workers sleep on `std::counting_semaphore` when idle; producers wake them via `release()` |
| **Mailbox full** | Messages are dropped with a warning. Deliberate design — favors availability over guaranteed delivery |
| **Dispatcher queue full** | Falls back to `pendingActorList_` (mutex-protected); retried during idle time |
| **Adaptive work stealing** | Workers transition from busy (200 μs) to idle (2000 μs) steal intervals to reduce CPU spin |
| **Mailbox capacity** | Configurable per actor at creation time; insufficient capacity causes throughput degradation |

---

## Summary

| Aspect | Detail |
|--------|--------|
| **Queue algorithms** | Vyukov MPSC (mailboxes) + Vyukov MPMC (work stealing) |
| **Hot path locks** | Zero — all enqueue/dispatch/acquire/run operations are lock-free |
| **Actor isolation** | Single-consumer mailbox + `scheduled_` gate = no actor-level locking |
| **Work distribution** | Deterministic affinity + load-aware dispatch + adaptive work stealing |
| **Semaphore wake-up** | `std::counting_semaphore` per worker — efficient OS-level sleep/wake |
| **Memory ordering** | Minimal: `relaxed` where possible, `acquire`/`release` for data publication, `seq_cst` only for the `scheduled_` gate |
| **Thread-local allocation** | TCMalloc-style three-tier allocator eliminates allocator contention |
| **False sharing prevention** | All hot-path atomics aligned to `kCacheLine` (64 or 128 bytes) |
| **Backpressure** | Drop-on-full (mailbox) + fallback queue (dispatcher) + adaptive backoff (stealing) |
