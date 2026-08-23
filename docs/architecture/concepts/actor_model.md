# Actor Model

---

## Table of Contents

- [Overview](#overview)
- [Design Philosophy](#design-philosophy)
- [Architecture](#architecture)
- [Core Classes](#core-classes)
  - [Actor](#actor)
  - [ActorSystem](#actorsystem)
  - [ActorHandle](#actorhandle)
  - [ActorRegistry](#actorregistry)
  - [ActorRuntime](#actorruntime)
- [Lifecycle](#lifecycle)
- [Communication](#communication)
  - [Message Sending](#message-sending)
  - [Type-Safe Dispatch](#type-safe-dispatch)
  - [Timers](#timers)
- [Fault Tolerance](#fault-tolerance)
- [Ownership & Hierarchy](#ownership--hierarchy)
- [Summary](#summary)

---

## Overview

V² Engine adopts a flat, isolated actor model for structuring long-running system daemons. Every component in the system — from CLI command handling to OS signal processing — is an independent actor that owns its state and communicates exclusively through asynchronous message passing.

Unlike game engine actor models (e.g., Unreal Engine's actor-component hierarchy), V² Engine actors are **self-contained peers** with no parent-child relationships and no spatial or transform system. This is a pure message-passing framework optimized for system-level services.

---

## Design Philosophy

| Principle | Description |
|-----------|-------------|
| **Actor isolation** | Each actor owns a private mailbox and processes messages sequentially. No shared state between actors. |
| **Lock-free hot path** | Mailboxes and dispatch queues use lock-free data structures. No mutex contention on the message delivery path. |
| **Cooperative scheduling** | Actors process messages in configurable batches (default: 32) then yield. No preemption. |
| **Compile-time safety** | The `dispatch()` template uses C++20 `requires` expressions to statically verify message handler coverage. |
| **Generation-based references** | `ActorHandle` uses generation counters to safely detect stale references without `shared_ptr` overhead. |

---

## Architecture

```
ActorSystem
  ├── ActorRegistry (name → ActorEntry, id → ActorEntry)
  ├── WorkDispatcher (per-worker LockFreeMpmcQueue<ActorRuntime*>)
  ├── Scheduler (timer → IActorRuntime* callbacks)
  ├── Supervisor (failure policy + DeadLetterQueue)
  ├── Worker[0..N] (threads)
  ├── ActorRuntime[0..M] (owns Actor + Mailbox each)
  │     ├── Actor (business logic)
  │     └── Mailbox (LockFreeMpscQueue)
  └── EventLoop (epoll)
```

**Ownership chain:** `ActorSystem` owns `Worker` threads and `ActorRuntime` instances. Each `ActorRuntime` wraps one `Actor` and one `Mailbox`. The `ActorRegistry` holds raw pointers for lookup (generation-checked). The `Supervisor` handles failures with configurable restart policies.

---

## Core Classes

### Actor

**File:** `src/core/actor_system/actor/actor.hpp:20`

Abstract base class for all actors. Defines the lifecycle interface and message-sending API.

```cpp
class Actor {
public:
    // --- Lifecycle hooks (pure virtual) ---
    virtual int  open() = 0;                    // Start the actor
    virtual int  close() = 0;                   // Stop the actor
    virtual void handle(const Message& msg) = 0; // Handle inbound message

    // --- Sending ---
    void sendMsg(const std::string& name, Message msg);
    void sendMsg(uint64_t id, Message msg);
    int  sendMsgAfter(const std::string& name, Message msg, uint64_t delayMs);
    int  sendMsgAfter(uint64_t id, Message msg, uint64_t delayMs);

    // --- Self-enqueue ---
    void receiveMsg(Message msg);

    // --- Timers ---
    int  startTimer(Message msg, uint64_t delayMs, bool repeating);
    void cancelTimer(int timerId);
    void cancelAllTimers();

    // --- Queries ---
    const std::string& name() const;
    uint64_t id() const;
    uint64_t generation() const;
    ActorState getState() const;
    bool isEssential() const;
    size_t mailboxCount() const;
    size_t mailboxCapacity() const;
};
```

**State enum:**

| State | Value | Description |
|-------|-------|-------------|
| `Closed` | 0 | Inactive, not processing messages |
| `Closing` | 1 | Transitioning to closed |
| `Opening` | 2 | Transitioning to open |
| `Opened` | 3 | Active, processing messages |
| `Inherited` | 4 | Special state for forwarding |

---

### ActorSystem

**File:** `src/core/actor_system/actor_system.hpp:50`

Top-level system that owns the runtime. Creates actors, manages worker threads, and coordinates the event loop.

```cpp
class ActorSystem {
public:
    // --- Actor creation (template factory) ---
    template<typename T, typename... Args>
    T* createActor(const std::string& name, size_t mailboxSize = 0, Args&&... args);

    // --- Lifecycle ---
    void start();        // Open all actors, start workers + event loop
    void stop();         // Drain, stop workers, close all actors
    void run();          // Block on event loop
    void requestStop();  // Signal event loop to stop
};
```

**Creation flow:**
1. Allocates unique ID via `nextActorId_++`
2. Constructs actor via `std::make_unique<T>(name, id, args...)`
3. Creates `Mailbox` + `ActorRuntime`, registers in `ActorRegistry`

---

### ActorHandle

**File:** `src/core/actor_system/actor/actor_handle.hpp:8`

Generation-aware weak reference to an actor. Prevents use-after-free when actor IDs are recycled.

```cpp
struct ActorHandle {
    bool    valid() const;          // Generation-check against registry
    Actor*  get() const;            // Resolve to raw pointer
    void    send(Message msg) const; // Send via resolved actor
    uint64_t id() const;
    uint64_t generation() const;
};
```

**How it works:** When an actor is removed from the registry, the generation counter for that ID slot is incremented. Any subsequent `handle.get()` call checks the generation and returns `nullptr` if the handle is stale. This eliminates the need for `shared_ptr` while remaining safe against dangling references.

---

### ActorRegistry

**File:** `src/core/actor_system/actor/actor_registry.hpp:10`

Thread-safe registry with dual indexing (by name and by ID).

```cpp
class ActorRegistry {
public:
    ActorHandle findHandleByName(const std::string& name);
    ActorHandle findHandleById(uint64_t id);
    Actor*      findActorByName(const std::string& name);
    Actor*      findActorById(uint64_t id);
    Actor*      resolve(const ActorHandle& handle) const; // generation-checked
    void        forEachActor(const std::function<void(ActorHandle)>& callback) const;
    void        add(Actor* actor);
    void        remove(Actor* actor);
};
```

---

### ActorRuntime

**File:** `src/core/actor_system/runtime/actor_runtime/actor_runtime.hpp:19`

Wraps an `Actor` and its `Mailbox`. Implements batch processing and integrates with the scheduler and supervisor.

```cpp
class ActorRuntime : public IActorRuntime {
public:
    void enqueue(Message msg) override;               // Push to mailbox + dispatch
    int  run(int maxBatch, bool* hasMoreWork);        // Process batch of messages
    int  addTimer(Actor* target, Message msg, uint64_t delayMs, bool repeating);
    bool tryRestart(const std::string& reason, int maxRestarts);
    void shutdown() override;
};
```

**Batch processing (`run`):**
1. Worker acquires `ActorRuntime*` from the dispatcher
2. Calls `processBatch(maxBatch)` — pops messages from mailbox
3. Checks `tryConsumeLifecycle()` first for system messages (`ActorEnableRequest`, `ActorDisableRequest`, `ActorRestartRequest`)
4. Calls `actor->handle(msg)` for application messages
5. If exception: notifies `Supervisor::onActorFailed()`
6. If mailbox not empty after batch: re-dispatches (work-conserving)

---

## Lifecycle

### State Machine

```
Closed → Opening → Opened → Closing → Closed
```

### Startup Sequence

1. `ActorSystem::start()` is called
2. `WorkDispatcher`, `EventLoop`, `Scheduler` are started
3. For each registered actor: `actor->open()` is called (state → `Opened`)
4. `Worker` threads are started

### Runtime Processing

Messages are processed cooperatively in batches:

```
semaphore → own queue pop → run(batch=32) → re-dispatch if non-empty
```

Actors yield after their batch, ensuring fairness across actors.

### Shutdown Sequence

1. `ActorSystem::stop()` is called
2. `Scheduler` and `EventLoop` are stopped
3. `WorkDispatcher::beginDrain()` — workers finish pending work then exit
4. All `Worker` threads are stopped
5. For each registered actor: `actor->close()` is called (state → `Closed`)

### Destruction

`ActorRuntime::~ActorRuntime()`:
1. Cancels all timers associated with this actor
2. Removes actor from registry (generation incremented, making stale handles invalid)

### Runtime Enable/Disable

| Message | Behavior |
|---------|----------|
| `ActorEnableRequest` | Calls `actor->open()` if state is `Closed` |
| `ActorDisableRequest` | Calls `actor->close()` if state is `Opened` AND actor is not essential |

Essential actors (`setEssential(true)`) cannot be disabled via messages.

---

## Communication

### Message Sending

All sending is **asynchronous and non-blocking** for the sender.

| Method | Target Resolution | Timing |
|--------|-------------------|--------|
| `sendMsg(name, msg)` | Registry lookup by name | Immediate |
| `sendMsg(id, msg)` | Registry lookup by ID | Immediate |
| `sendMsgAfter(name, msg, delayMs)` | Registry lookup by name | Delayed via Scheduler |
| `sendMsgAfter(id, msg, delayMs)` | Registry lookup by ID | Delayed via Scheduler |

**Message flow:**

```
Sender Actor
  → sendMsg("target", msg)
  → Runtime resolves ActorHandle (name → id via registry)
  → Runtime enqueues into target's Mailbox (lock-free MPSC push)
  → Runtime notifies WorkDispatcher (dispatch)
  → WorkDispatcher pushes ActorRuntime* to target worker's queue
  → Worker acquires (semaphore wake or steal)
  → ActorRuntime::run(maxBatch) processes messages
  → Actor::handle(msg) is called
```

### Type-Safe Dispatch

The `dispatch()` template method provides compile-time verified message routing using C++20 `requires` expressions:

```cpp
// Define message tuple for this actor type
using CmdActorMessages = std::tuple<CmdRequest, PmuDataUpdate, WifiScanResult, ...>;

void CmdActor::handle(const Message& msg) {
    Actor::dispatch(*this, msg, CmdActorMessages{});  // compile-time checked
}

// Overloaded handlers for each concrete type
void CmdActor::handle(const CmdRequest& m) { ... }
void CmdActor::handle(const PmuDataUpdate& m) { ... }
```

If a message type appears in the tuple but lacks a corresponding `handle(T)` overload, the code fails to compile — catching missing handlers at build time rather than runtime.

### Timers

```cpp
int  startTimer(Message msg, uint64_t delayMs, bool repeating);
void cancelTimer(int timerId);
void cancelAllTimers();
```

- Timer callbacks clone the message and enqueue it into the target actor's mailbox
- `ActorRuntime::~ActorRuntime()` automatically cancels all timers

---

## Fault Tolerance

### Supervisor

**File:** `src/core/actor_system/runtime/supervisor/supervisor.hpp:30`

| Strategy | Behavior |
|----------|----------|
| `OneForOne` | Restart only the failed actor (up to `maxRestarts`) |
| `OneForAll` | Broadcast `ActorRestartRequest` to ALL actors |
| `None` | Permanent shutdown of the failed actor |

### Failure Handling Flow

When `onActorFailed()` is called during message processing:

1. **Dead Letter**: The failed message + all remaining mailbox messages are moved to `DeadLetterQueue`
2. **Strategy Application**:
   - `OneForOne`: `runtime->tryRestart(reason, limit)` with atomic CAS on restart counter
   - `OneForAll`: Broadcasts `ActorRestartRequest` to all actors
   - `None`: Calls `runtime->shutdown()` permanently
3. **Budget Check**: If max restarts exceeded, actor is permanently shut down

### Per-Actor Policy Override

```cpp
supervisor->setStrategy(actorId, RestartStrategy::None);  // disable restart for specific actor
supervisor->setDefaultStrategy(RestartStrategy::OneForAll); // system-wide default
```

---

## Ownership & Hierarchy

V² Engine actors are **flat peers** — there is no parent-child hierarchy.

```
ActorSystem
  ├── Actor A (standalone)
  ├── Actor B (standalone)
  ├── Actor C (standalone)
  └── ...
```

Each actor is:
- Registered in a single global `ActorRegistry`
- Independent of all other actors
- Communicating exclusively through asynchronous messages

This flat design simplifies reasoning about actor state and eliminates the complexity of hierarchical lifecycle management. Dependencies between actors are expressed through message protocols, not ownership.

---

## Summary

| Aspect | Detail |
|--------|--------|
| **Base class** | `Actor` — abstract, 3 pure virtual methods (`open`, `close`, `handle`) |
| **Identification** | Name (string) + ID (uint64_t), with generation-based safe handles |
| **Mailbox** | Lock-free MPSC queue, one per actor |
| **Scheduling** | Cooperative batch processing (default batch = 32) |
| **Message dispatch** | Compile-time verified via `dispatch()` + `requires` expressions |
| **Fault tolerance** | Supervisor with OneForOne / OneForAll / None strategies |
| **Hierarchy** | Flat — no parent-child relationships |
| **Thread safety** | All hot-path operations are lock-free; no mutex contention |
