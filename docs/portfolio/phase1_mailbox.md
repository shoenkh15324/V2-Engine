# Phase 1: Mailbox Variants Implementation

> **Duration**: Week 1-6 (4-6 weeks)
> **Goal**: Implement multiple mailbox variants (Mutex, MPSC Lock-free) with identical interfaces
> **Prerequisite**: None (can start immediately)

---

## Table of Contents

1. [Objectives](#1-objectives)
2. [Current Mailbox Analysis](#2-current-mailbox-analysis)
3. [IMailbox Interface Design](#3-imailbox-interface-design)
4. [MutexMailbox (Baseline)](#4-mutexmailbox-baseline)
5. [MPSCMailbox (Lock-free)](#5-mpscmailbox-lock-free)
6. [ActorContext Refactoring](#6-actorcontext-refactoring)
7. [MailboxType Selection](#7-mailboxtype-selection)
8. [Unit Tests](#8-unit-tests)
9. [Implementation Steps](#9-implementation-steps)
10. [Verification Checklist](#10-verification-checklist)

---

## 1. Objectives

### 1.1 Primary Objectives

1. Extract `IMailbox<T>` interface from current `Mailbox<T>`
2. Rename current `Mailbox<T>` to `MutexMailbox<T>` (no logic changes)
3. Implement `MPSCMailbox<T>` using lock-free bounded queue with sequence counters
4. Refactor `ActorContext` to use `IMailbox<T>` instead of concrete `Mailbox<T>`
5. Add `MailboxType` enum for runtime mailbox selection
6. Write comprehensive unit tests for all variants

### 1.2 Learning Outcomes (Portfolio Value)

| Skill | Demonstrates |
|-------|--------------|
| Lock-free data structures | Deep understanding of atomic operations and memory ordering |
| C++20 atomics | Modern C++ concurrency primitives |
| Interface design | Polymorphic behavior with zero-cost abstractions |
| Testing concurrent code | Stress testing, sanitizer usage, correctness verification |
| Performance trade-offs | Empirical comparison of synchronization strategies |

---

## 2. Current Mailbox Analysis

### 2.1 Source Code Review

**File**: `src/core/actor_system/runtime/mailbox.hpp`

```cpp
template <typename T>
class Mailbox{
public:
    explicit Mailbox(size_t size) : buffer_(size), capacity_(size), count_(0), head_(0), tail_(0){}

    template <typename U>
    bool push(U&& msg){
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ == capacity_){
            return false;
        }
        buffer_[tail_] = std::forward<U>(msg);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return true;
    }

    bool pop(T& out){
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ == 0){
            return false;
        }
        out = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        --count_;
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == 0;
    }

    size_t capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return capacity_;
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    std::vector<T> buffer_;
    mutable std::mutex mutex_;
    size_t capacity_;
    size_t count_;
    size_t head_;
    size_t tail_;
};
```

### 2.2 Concurrency Pattern Analysis

**Producer Side (push)**:
- Called from `ActorContext::enqueue()` (multiple threads)
- Access pattern: MPSC (Multiple Producer)
- Current protection: `std::mutex`

**Consumer Side (pop)**:
- Called from `ActorContext::run()` (single worker thread guaranteed by `scheduled_` flag)
- Access pattern: SC (Single Consumer)
- Current protection: `std::mutex`

**Conclusion**: The mailbox is **MPSC** (Multi-Producer, Single-Consumer), not SPSC.

### 2.3 Performance Issues

| Issue | Location | Overhead |
|-------|----------|----------|
| Mutex lock/unlock per push | `push()` line 18 | ~20-40ns per operation |
| Mutex lock/unlock per pop | `pop()` line 29 | ~20-40ns per operation |
| Double mutex acquisition | `enqueue()` calls `push()` then `count()` | 2x overhead |
| Cache-line bouncing | All threads access same mutex cache line | Scales with core count |

### 2.4 Usage Sites

| File | Method | Context |
|------|--------|---------|
| `actor_context.cpp:22` | `mailbox_.push()` | enqueue() |
| `actor_context.cpp:26` | `mailbox_.count()` | metrics recording |
| `actor_context.cpp:37` | `mailbox_.pop()` | run() batch loop |
| `actor_context.cpp:46` | `mailbox_.empty()` | re-dispatch check |

---

## 3. IMailbox Interface Design

### 3.1 Interface Definition

**New file**: `src/core/actor_system/runtime/i_mailbox.hpp`

```cpp
#pragma once
#include <cstddef>

template <typename T>
class IMailbox {
public:
    virtual ~IMailbox() = default;

    // Push a message into the mailbox.
    // Returns true if successful, false if mailbox is full.
    virtual bool push(T&& msg) = 0;

    // Pop a message from the mailbox.
    // Returns true if successful, false if mailbox is empty.
    virtual bool pop(T& out) = 0;

    // Check if the mailbox is empty.
    // Note: For lock-free variants, this may be approximate.
    virtual bool empty() const = 0;

    // Get current number of messages in the mailbox.
    // Note: For lock-free variants, this may be approximate.
    virtual size_t count() const = 0;

    // Get maximum capacity of the mailbox.
    virtual size_t capacity() const = 0;
};
```

### 3.2 Design Rationale

**Why virtual?**
- Runtime polymorphism allows benchmarking different implementations without code changes
- Clean separation of interface from implementation
- Virtual dispatch overhead (~2ns) is negligible compared to synchronization overhead

**Why `T&&` for push?**
- Move semantics avoid copying large `std::variant<Message>` objects
- Universal reference `U&&` (from current implementation) can be preserved if needed
- Consistent with current API

**Why approximate `count()` and `empty()`?**
- Lock-free implementations cannot provide exact counts without synchronization
- Current usage (metrics, re-dispatch check) tolerates approximate values
- Consumer-side reads are naturally consistent

### 3.3 Alternative: CRTP (Static Polymorphism)

For production use where virtual overhead matters:

```cpp
template <typename Derived, typename T>
class IMailboxCRTP {
public:
    bool push(T&& msg) { return static_cast<Derived*>(this)->pushImpl(std::move(msg)); }
    bool pop(T& out) { return static_cast<Derived*>(this)->popImpl(out); }
    bool empty() const { return static_cast<Derived*>(this)->emptyImpl(); }
    size_t count() const { return static_cast<Derived*>(this)->countImpl(); }
    size_t capacity() const { return static_cast<Derived*>(this)->capacityImpl(); }
};
```

**Decision**: Use virtual interface for benchmarking phase. Consider CRTP for production optimization later.

---

## 4. MutexMailbox (Baseline)

### 4.1 Implementation Plan

**File**: `src/core/actor_system/runtime/mutex_mailbox.hpp`

- Copy current `mailbox.hpp` content
- Rename class from `Mailbox<T>` to `MutexMailbox<T>`
- Implement `IMailbox<T>` interface
- Keep all existing logic unchanged (proven correct)

### 4.2 Code Structure

```cpp
#pragma once
#include "i_mailbox.hpp"
#include <vector>
#include <mutex>

template <typename T>
class MutexMailbox : public IMailbox<T> {
public:
    explicit MutexMailbox(size_t size)
        : buffer_(size), capacity_(size), count_(0), head_(0), tail_(0) {}

    bool push(T&& msg) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ == capacity_) {
            return false;
        }
        buffer_[tail_] = std::move(msg);
        tail_ = (tail_ + 1) % capacity_;
        ++count_;
        return true;
    }

    bool pop(T& out) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if(count_ == 0) {
            return false;
        }
        out = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        --count_;
        return true;
    }

    bool empty() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == 0;
    }

    size_t count() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    size_t capacity() const override {
        return capacity_;
    }

private:
    std::vector<T> buffer_;
    mutable std::mutex mutex_;
    const size_t capacity_;
    size_t count_;
    size_t head_;
    size_t tail_;
};
```

### 4.3 Changes from Original

| Change | Reason |
|--------|--------|
| Class name `Mailbox` → `MutexMailbox` | Distinguish from interface and other variants |
| `implements IMailbox<T>` | Interface compliance |
| `capacity_` marked `const` | Never changes after construction, no need for mutex in `capacity()` |
| `push()` takes `T&&` instead of template `U&&` | Interface compliance (can add overload if needed) |
| `buffer_[tail_] = std::move(msg)` instead of `std::forward` | Clearer intent |

---

## 5. MPSCMailbox (Lock-free)

### 5.1 Design Overview

Based on Dmitry Vyukov's bounded MPMC queue, simplified for MPSC:

- **Multiple producers** can concurrently push
- **Single consumer** reads exclusively
- **Bounded capacity**: returns false when full
- **No CAS on push**: uses `fetch_add` for slot claiming
- **Sequence counters**: ensure safe slot access

### 5.2 Data Structure

```cpp
template <typename T>
class MPSCMailbox : public IMailbox<T> {
private:
    struct Slot {
        std::atomic<size_t> sequence;
        T data;

        Slot() : sequence(0) {}
    };

    std::vector<Slot> buffer_;
    const size_t capacity_;

    // Cache-line aligned to prevent false sharing
    alignas(64) std::atomic<size_t> head_{0};  // Producer index (multiple producers)
    alignas(64) std::atomic<size_t> tail_{0};  // Consumer index (single consumer)

    // Cache line size constant
    static constexpr size_t CACHE_LINE_SIZE = 64;
};
```

### 5.3 Push Implementation (MPSC)

```cpp
bool push(T&& msg) override {
    // Step 1: Atomically claim a slot index
    size_t pos = head_.fetch_add(1, std::memory_order_relaxed);

    // Step 2: Check if we've exceeded capacity
    // Note: Multiple producers may overshoot, but they'll wait on sequence
    size_t current_tail = tail_.load(std::memory_order_acquire);

    if(pos - current_tail >= capacity_) {
        // We've gone past capacity. We need to "undo" our claim.
        // However, fetch_add is not reversible in lock-free MPSC.
        // Instead, we must wait for the consumer to catch up.
        // This is the trade-off: we spin-wait here.
        while(head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire) >= capacity_) {
            // Spin-wait for consumer to make space
            // In practice, this rarely happens if capacity is sufficient
        }
    }

    // Step 3: Wait for this slot to become available (sequence == pos)
    size_t slot_idx = pos % capacity_;
    while(buffer_[slot_idx].sequence.load(std::memory_order_acquire) != pos) {
        // Spin-wait for consumer to release this slot
    }

    // Step 4: Write data into the slot
    buffer_[slot_idx].data = std::move(msg);

    // Step 5: Publish the slot (make it visible to consumer)
    buffer_[slot_idx].sequence.store(pos + 1, std::memory_order_release);

    return true;
}
```

### 5.4 Alternative Push Design (Avoiding Overshoot)

The above design has a problem: `fetch_add` can advance head beyond capacity, causing spin-wait. Better approach:

```cpp
bool push(T&& msg) override {
    // Approach: Reserve-then-validate with backoff
    while(true) {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_acquire);

        // Check if full
        if(h - t >= capacity_) {
            return false;  // Mailbox full, drop message (consistent with current behavior)
        }

        // Try to claim this slot
        if(head_.compare_exchange_weak(h, h + 1, std::memory_order_relaxed)) {
            // Successfully claimed slot h
            size_t slot_idx = h % capacity_;

            // Wait for slot to be ready (consumer has finished with it)
            while(buffer_[slot_idx].sequence.load(std::memory_order_acquire) != h) {
                // Spin-wait
            }

            // Write data
            buffer_[slot_idx].data = std::move(msg);

            // Publish
            buffer_[slot_idx].sequence.store(h + 1, std::memory_order_release);
            return true;
        }
        // CAS failed, retry
    }
}
```

**Trade-off Analysis**:

| Approach | Pros | Cons |
|----------|------|------|
| `fetch_add` + spin-wait | Simple, fast when not full | Can overshoot, requires spin-wait |
| CAS loop | No overshoot, returns false when full | CAS retry overhead, slightly slower |

**Decision**: Use CAS approach for consistency with current `push()` semantics (returns false when full).

### 5.5 Pop Implementation (Single Consumer)

```cpp
bool pop(T& out) override {
    size_t t = tail_.load(std::memory_order_relaxed);
    size_t h = head_.load(std::memory_order_acquire);

    // Check if empty
    if(t == h) {
        return false;  // No messages available
    }

    // Wait for slot to be ready (producer has finished writing)
    size_t slot_idx = t % capacity_;
    while(buffer_[slot_idx].sequence.load(std::memory_order_acquire) != t + 1) {
        // Spin-wait for producer to publish
        // In practice, this spin is very short (producer writes immediately after CAS)
    }

    // Move data out of slot
    out = std::move(buffer_[slot_idx].data);

    // Advance tail (signals to producers that this slot is available)
    tail_.store(t + 1, std::memory_order_release);

    return true;
}
```

**Key Insight**: The consumer spin-wait is extremely short because:
1. The producer claimed slot `t` via CAS
2. The producer writes data immediately
3. The producer publishes via `sequence.store(t+1, release)`
4. The consumer only waits for step 2+3 (data write + release store)

### 5.6 Empty and Count Implementation

```cpp
bool empty() const override {
    // Approximate: may see stale values
    // But safe for consumer-side use (re-dispatch check)
    return tail_.load(std::memory_order_acquire) ==
           head_.load(std::memory_order_acquire);
}

size_t count() const override {
    // Approximate: may see stale values
    // Safe for metrics (informational only)
    size_t h = head_.load(std::memory_order_acquire);
    size_t t = tail_.load(std::memory_order_acquire);
    return (h >= t) ? (h - t) : 0;
}
```

### 5.7 Memory Ordering Summary

| Operation | Order | Reason |
|-----------|-------|--------|
| `head_.fetch_add` / CAS | relaxed (success) | Only need atomicity, not ordering |
| `tail_.load` in push | acquire | See consumer's tail advance |
| `buffer_[idx].sequence.load` in push | acquire | See consumer's release |
| `buffer_[idx].sequence.store` in push | release | Publish data to consumer |
| `head_.load` in pop | acquire | See producer's head advance |
| `buffer_[idx].sequence.load` in pop | acquire | See producer's release |
| `tail_.store` in pop | release | Signal slot availability to producers |
| `head_.load` in empty/count | acquire | See latest head |
| `tail_.load` in empty/count | acquire | See latest tail |

### 5.8 ABA Problem Analysis

**Is ABA a problem here?**

- **Head (producer index)**: Monotonically increasing via `fetch_add` or CAS. No ABA.
- **Tail (consumer index)**: Monotonically increasing via `store`. No ABA.
- **Sequence counters**: Monotonically increasing. No ABA.

**Conclusion**: No ABA problem because all indices are monotonically increasing and never wrap around to the same value (size_t overflow at 2^64 is not a practical concern).

### 5.9 False Sharing Prevention

```cpp
// Producer-side index: only written by producers
alignas(64) std::atomic<size_t> head_{0};

// Consumer-side index: only written by consumer
alignas(64) std::atomic<size_t> tail_{0};
```

**Why `alignas(64)`?**
- Modern x86 cache lines are 64 bytes
- Without padding, `head_` and `tail_` would share a cache line
- When producer writes `head_` and consumer reads it, the cache line bounces between cores
- Padding ensures independent cache lines, eliminating false sharing

---

## 6. ActorContext Refactoring

### 6.1 Current Code

**File**: `src/core/actor_system/actor/actor_context.hpp`

```cpp
class ActorContext {
private:
    std::unique_ptr<Actor> actor_;
    Mailbox<Message> mailbox_;  // <-- Concrete type
    std::atomic<bool> scheduled_{false};
    Dispatcher* dispatcher_ = nullptr;
    // ...
};
```

### 6.2 Target Code

```cpp
class ActorContext {
private:
    std::unique_ptr<Actor> actor_;
    std::unique_ptr<IMailbox<Message>> mailbox_;  // <-- Interface pointer
    std::atomic<bool> scheduled_{false};
    Dispatcher* dispatcher_ = nullptr;
    // ...
};
```

### 6.3 Constructor Changes

**Current**:
```cpp
ActorContext::ActorContext(std::unique_ptr<Actor> actor, size_t mailboxSize, ...)
    : actor_(std::move(actor)), mailbox_(mailboxSize) {
    // ...
}
```

**Target**:
```cpp
ActorContext::ActorContext(std::unique_ptr<Actor> actor, size_t mailboxSize,
                          MailboxType type, ...) 
    : actor_(std::move(actor)), mailbox_(createMailbox<Message>(type, mailboxSize)) {
    // ...
}
```

### 6.4 Mailbox Factory Function

```cpp
// In i_mailbox.hpp or a separate factory header
template <typename T>
std::unique_ptr<IMailbox<T>> createMailbox(MailboxType type, size_t capacity) {
    switch(type) {
        case MailboxType::Mutex:
            return std::make_unique<MutexMailbox<T>>(capacity);
        case MailboxType::MPSC:
            return std::make_unique<MPSCMailbox<T>>(capacity);
        default:
            return std::make_unique<MutexMailbox<T>>(capacity);
    }
}
```

### 6.5 Impact on Existing Code

| File | Change Required |
|------|-----------------|
| `actor_context.hpp` | Change `Mailbox<Message>` to `std::unique_ptr<IMailbox<Message>>` |
| `actor_context.cpp` | Update constructor to accept `MailboxType` |
| `actor_system.hpp` | Add `MailboxType` member, pass to ActorContext |
| `actor_system.cpp` | Accept `MailboxType` in constructor |
| `bench_throughput.cpp` | Update to pass `MailboxType` to ActorSystem |
| All test files using ActorContext | Update constructor calls |

### 6.6 Backward Compatibility

To minimize impact on existing code:

1. **Default parameter**: `MailboxType type = MailboxType::Mutex` in ActorSystem constructor
2. **Alias**: Keep `using Mailbox<T> = MutexMailbox<T>` for legacy code
3. **Gradual migration**: Update test files one by one

---

## 7. MailboxType Selection

### 7.1 Enum Definition

**File**: `src/core/actor_system/runtime/i_mailbox.hpp`

```cpp
enum class MailboxType {
    Mutex,      // MutexMailbox: proven correct, simple, baseline
    MPSC,       // MPSCMailbox: lock-free, high throughput
    // Future:
    // SPSC,    // SPSCMailbox: lock-free, single producer only
};
```

### 7.2 ActorSystem Configuration

**File**: `src/core/actor_system/actor_system.hpp`

```cpp
class ActorSystem {
public:
    ActorSystem(int workerCount, int maxBatch,
                MailboxType mailboxType = MailboxType::Mutex);
    // ...
private:
    MailboxType mailboxType_;
};
```

### 7.3 CLI Integration

**File**: `src/app/cli/cli_app.cpp`

Add `--mailbox` flag to benchmark commands:

```
v2 benchmark throughput --actors 16 --workers 64 --mailbox mpsc
v2 benchmark comparison --actors 16 --workers 64
```

### 7.4 RuntimeConfig Integration

**File**: `src/core/common/config/runtime_config.h`

```cpp
struct RuntimeConfig {
    // ...
    MailboxType mailboxType = MailboxType::Mutex;
    // ...
};
```

---

## 8. Unit Tests

### 8.1 Test File Structure

**New file**: `test/unit/core/test_mpsc_mailbox.cpp`

```cpp
#include <gtest/gtest.h>
#include "core/actor_system/runtime/mpsc_mailbox.hpp"

class MPSCMailboxTest : public ::testing::Test {
protected:
    static constexpr size_t CAPACITY = 1024;
    MPSCMailbox<int> mailbox{CAPACITY};
};

// Basic operations
TEST_F(MPSCMailboxTest, PushPopSingle) { ... }
TEST_F(MPSCMailboxTest, PushFIFO) { ... }
TEST_F(MPSCMailboxTest, PopEmptyReturnsFalse) { ... }
TEST_F(MPSCMailboxTest, PushFullReturnsFalse) { ... }
TEST_F(MPSCMailboxTest, CountMatchesPushPop) { ... }
TEST_F(MPSCMailboxTest, EmptyAfterDrain) { ... }
TEST_F(MPSCMailboxTest, CapacityMatches) { ... }

// Concurrency tests
TEST_F(MPSCMailboxTest, SingleProducerSingleConsumer) { ... }
TEST_F(MPSCMailboxTest, MultipleProducerSingleConsumer) { ... }
TEST_F(MPSCMailboxTest, StressTestNoDroppedMessages) { ... }
TEST_F(MPSCMailboxTest, StressTestNoCorruption) { ... }

// Edge cases
TEST_F(MPSCMailboxTest, FillAndDrain) { ... }
TEST_F(MPSCMailboxTest, InterleavedPushPop) { ... }
TEST_F(MPSCMailboxTest, SingleSlot) { ... }
TEST_F(MPSCMailboxTest, LargeMessages) { ... }
```

### 8.2 Test Scenarios Detail

#### 8.2.1 Basic Operations

| Test | Description | Expected |
|------|-------------|----------|
| `PushPopSingle` | Push one, pop one | Same value returned |
| `PushFIFO` | Push 1,2,3; pop should return 1,2,3 | FIFO order preserved |
| `PopEmptyReturnsFalse` | Pop from empty mailbox | Returns false |
| `PushFullReturnsFalse` | Push to capacity+1 | Returns false on last push |
| `CountMatchesPushPop` | Push N, pop N | count() matches after each op |
| `EmptyAfterDrain` | Push N, pop N | empty() returns true |
| `CapacityMatches` | Constructor with capacity C | capacity() returns C |

#### 8.2.2 Concurrency Tests

| Test | Description | Expected |
|------|-------------|----------|
| `SingleProducerSingleConsumer` | 1 producer thread, 1 consumer thread | All messages received in order |
| `MultipleProducerSingleConsumer` | 4 producer threads, 1 consumer thread | All messages received (order may vary per producer) |
| `StressTestNoDroppedMessages` | 100K messages across 8 producers | Total received == total sent |
| `StressTestNoCorruption` | Verify message integrity after concurrent access | All values match sent values |

#### 8.2.3 Stress Test Implementation Sketch

```cpp
TEST_F(MPSCMailboxTest, StressTestNoDroppedMessages) {
    constexpr int NUM_PRODUCERS = 8;
    constexpr int MSGS_PER_PRODUCER = 10000;
    constexpr int TOTAL = NUM_PRODUCERS * MSGS_PER_PRODUCER;

    std::atomic<int> received{0};
    std::vector<int> received_values;
    std::mutex values_mutex;

    // Producer threads
    std::vector<std::thread> producers;
    for(int p = 0; p < NUM_PRODUCERS; p++) {
        producers.emplace_back([&, p] {
            for(int i = 0; i < MSGS_PER_PRODUCER; i++) {
                while(!mailbox.push(p * MSGS_PER_PRODUCER + i)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Consumer thread
    std::thread consumer([&] {
        int val;
        while(received.load() < TOTAL) {
            if(mailbox.pop(val)) {
                std::lock_guard lock(values_mutex);
                received_values.push_back(val);
                received.fetch_add(1);
            }
        }
    });

    for(auto& t : producers) t.join();
    consumer.join();

    EXPECT_EQ(received.load(), TOTAL);
    EXPECT_EQ(received_values.size(), TOTAL);

    // Verify no duplicates (optional, depending on design)
    // Note: With MPSC, messages from same producer are ordered,
    // but messages across producers may interleave
}
```

### 8.3 Interface Compatibility Tests

**New file**: `test/unit/core/test_mailbox_interface.cpp`

Test that all variants behave identically:

```cpp
class MailboxInterfaceTest : public ::testing::TestWithParam<MailboxType> {
protected:
    std::unique_ptr<IMailbox<int>> createMailbox(size_t cap) {
        return createMailbox<int>(GetParam(), cap);
    }
};

TEST_P(MailboxInterfaceTest, PushPopBehavior) { ... }
TEST_P(MailboxInterfaceTest, FIFOOrdering) { ... }
TEST_P(MailboxInterfaceTest, FullBehavior) { ... }
TEST_P(MailboxInterfaceTest, EmptyBehavior) { ... }

INSTANTIATE_TEST_SUITE_P(
    AllMailboxes,
    MailboxInterfaceTest,
    ::testing::Values(MailboxType::Mutex, MailboxType::MPSC)
);
```

### 8.4 AddressSanitizer and ThreadSanitizer

All tests must pass with sanitizers enabled:

```bash
# Debug build with sanitizers
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined,thread" \
      ..

# Run tests
ctest --output-on-failure
```

**Expected**: Zero sanitizer errors (no data races, no memory errors, no undefined behavior).

---

## 9. Implementation Steps

### Week 1: Interface and MutexMailbox

| Day | Task | Deliverable |
|-----|------|-------------|
| 1 | Create `i_mailbox.hpp` with IMailbox interface | Interface file |
| 2 | Create `mutex_mailbox.hpp` implementing IMailbox | Renamed mutex mailbox |
| 3 | Create `MailboxType` enum and factory function | Factory utility |
| 4 | Update `ActorContext` to use `IMailbox<T>` | Refactored actor_context |
| 5 | Update `ActorSystem` to accept `MailboxType` | Updated actor_system |
| 6-7 | Fix compilation errors, update all call sites | Working build |

### Week 2: MPSCMailbox Core Implementation

| Day | Task | Deliverable |
|-----|------|-------------|
| 8-9 | Implement MPSCMailbox push/pop with sequence counters | Core lock-free logic |
| 10 | Add cache-line padding and memory ordering | Optimized implementation |
| 11-12 | Handle edge cases (full, empty, single slot) | Complete implementation |
| 13-14 | Basic unit tests (non-concurrent) | Passing basic tests |

### Week 3: MPSCMailbox Concurrency Testing

| Day | Task | Deliverable |
|-----|------|-------------|
| 15-16 | Concurrent unit tests (SPSC, MPSC scenarios) | Concurrency tests |
| 17 | Stress tests with high message counts | Stress tests |
| 18 | ThreadSanitizer validation | TSan clean |
| 19-20 | Debug and fix any issues found | Bug fixes |

### Week 4: Integration

| Day | Task | Deliverable |
|-----|------|-------------|
| 21-22 | Integrate MPSCMailbox into benchmark framework | Working benchmark |
| 23 | Compare Mutex vs MPSC throughput | Initial comparison data |
| 24-25 | Update existing tests to use interface | All tests passing |
| 26-27 | Documentation and code review | Clean code |

### Week 5-6: Polish and Edge Cases

| Day | Task | Deliverable |
|-----|------|-------------|
| 28-30 | Edge case testing (capacity=1, large messages, etc.) | Edge case coverage |
| 31-33 | Performance profiling and optimization | Optimized implementation |
| 34-35 | Code cleanup and refactoring | Production-ready code |
| 36-37 | Update README with mailbox variants | Updated documentation |
| 38-40 | Final review and testing | Phase 1 complete |

---

## 10. Verification Checklist

### 10.1 Code Quality

- [ ] All existing unit tests pass with MutexMailbox
- [ ] All existing unit tests pass with MPSCMailbox
- [ ] No compiler warnings with `-Wall -Wextra -Wpedantic`
- [ ] No AddressSanitizer errors
- [ ] No ThreadSanitizer errors
- [ ] No UndefinedBehaviorSanitizer errors
- [ ] Code follows existing style (indentation, naming, etc.)

### 10.2 Functionality

- [ ] `IMailbox<T>` interface is complete
- [ ] `MutexMailbox<T>` implements all interface methods
- [ ] `MPSCMailbox<T>` implements all interface methods
- [ ] `createMailbox<T>()` factory works for all types
- [ ] `ActorContext` correctly uses interface pointer
- [ ] `ActorSystem` correctly passes `MailboxType`
- [ ] Benchmark commands support `--mailbox` flag

### 10.3 Correctness

- [ ] FIFO ordering preserved in both variants
- [ ] Full mailbox returns false on push
- [ ] Empty mailbox returns false on pop
- [ ] Concurrent MPSC: no lost messages
- [ ] Concurrent MPSC: no duplicated messages
- [ ] Concurrent MPSC: no data corruption
- [ ] Concurrent MPSC: no deadlocks

### 10.4 Performance

- [ ] MutexMailbox baseline established
- [ ] MPSCMailbox shows improvement under contention
- [ ] MPSCMailbox shows no regression in low-contention
- [ ] Memory usage comparable between variants

### 10.5 Documentation

- [ ] Interface documented with comments
- [ ] MPSCMailbox design rationale documented
- [ ] Memory ordering choices documented
- [ ] Unit test coverage > 90% for both variants

---

## Appendix: Lock-free Reference Material

### A.1 Memory Ordering Cheatsheet

| Order | Equivalent To | Use Case |
|-------|---------------|----------|
| `relaxed` | No ordering | Counters, statistics |
| `acquire` | Load with acquire semantics | Reading shared data after producer writes |
| `release` | Store with release semantics | Writing shared data before publishing |
| `acq_rel` | Acquire + Release | Read-modify-write operations |
| `seq_cst` | Full serialization | When unsure (avoid if possible) |

### A.2 Common Pitfalls

1. **Missing acquire/release**: Data written by producer not visible to consumer
2. **Incorrect ordering**: Reading stale values due to relaxed loads where acquire needed
3. **False sharing**: Performance degradation from cache-line bouncing
4. **ABA problem**: Not applicable here (monotonic indices), but important to verify
5. **Spurious wakeups**: Not applicable (no condition variables), but worth noting

### A.3 Testing Strategies

1. **Stress testing**: High message counts with multiple threads
2. **Interleaving testing**: Vary thread priorities/scheduling to explore interleavings
3. **Sanitizer testing**: TSan for data races, ASan for memory errors
4. **Invariant checking**: Verify count, FIFO order, no duplicates
5. **Chaos testing**: Random timing, random message sizes
