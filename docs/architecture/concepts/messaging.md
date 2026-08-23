# Messaging

---

## Table of Contents

- [Overview](#overview)
- [Design Principles](#design-principles)
- [Message Class](#message-class)
  - [Storage Layout](#storage-layout)
  - [Storage Modes](#storage-modes)
  - [Type-Erased Construction](#type-erased-construction)
  - [Move Semantics](#move-semantics)
  - [Cloning](#cloning)
- [MessageOps Vtable](#messageops-vtable)
- [MessageId & Traits](#messageid--traits)
- [Type-Safe Dispatch](#type-safe-dispatch)
  - [Visit Pattern](#visit-pattern)
  - [Compile-Time Handler Verification](#compile-time-handler-verification)
  - [Dead Letter Handling](#dead-letter-handling)
- [Core Messages](#core-messages)
- [Message Catalog](#message-catalog)
  - [Core Lifecycle](#core-lifecycle)
  - [Tick](#tick)
  - [Command (Cmd)](#command-cmd)
  - [IPC](#ipc)
  - [Monitor](#monitor)
  - [D-Bus](#d-bus)
  - [Network Manager](#network-manager)
  - [Wi-Fi](#wi-fi)
  - [Device Manager (PMU)](#device-manager-pmu)
  - [System Manager](#system-manager)
- [Communication Patterns](#communication-patterns)
  - [Request-Response](#request-response)
  - [Pub-Sub (Retained-Latest)](#pub-sub-retained-latest)
  - [Timer-Driven Periodic](#timer-driven-periodic)
  - [Delayed Message Delivery](#delayed-message-delivery)
  - [Lifecycle Messages (System-Intercepted)](#lifecycle-messages-system-intercepted)
  - [Event Loop Bridge](#event-loop-bridge)
- [Message Flow (End-to-End)](#message-flow-end-to-end)
- [Summary](#summary)

---

## Overview

V² Engine uses a **type-erased, SBO-optimized, move-only** message system as the universal communication mechanism between actors. Every interaction — from CLI command handling to timer-driven heartbeats — flows through typed message structs that are wrapped into a uniform `Message` container.

The system is designed for **zero-heap-allocation on the hot path**: messages ≤ 64 bytes are stored inline within the `Message` object itself. Combined with the lock-free MPSC mailbox, this achieves **7.1M msgs/sec** throughput and **P50 = 378 ns** latency.

---

## Design Principles

| Principle | Description |
|-----------|-------------|
| **Type erasure with zero overhead** | `Message` wraps any payload type without virtual dispatch. A compile-time vtable (`MessageOps`) replaces RTTI. |
| **Small Buffer Optimization (SBO)** | Messages ≤ 64 bytes are stored inline — no heap allocation. Covers most message types in the system. |
| **Move-only** | Messages are moved, never copied. `clone()` is available explicitly for timer callbacks that need duplicates. |
| **Compile-time safety** | `dispatch()` uses C++20 `requires` expressions to verify at build time that all message types have handlers. |
| **Uniform discriminator** | Every payload type declares `static constexpr MessageId kId` — a single `uint32_t` enum used for runtime routing. |

---

## Message Class

**File:** `src/core/actor_system/messages/message.hpp`

### Storage Layout

```
Message (72 bytes) {
    MessageId          id_;        // 4 bytes  — type discriminator
    StorageMode        mode_;      // 1 byte   — Empty | Inline | Pool
    const MessageOps*  ops_;       // 8 bytes  — vtable pointer
    IMemoryAllocator*  allocator_; // 8 bytes  — pool allocator (for Pool mode)
    union {                        // 64 bytes — inline buffer or heap pointer
        alignas(max_align_t) std::byte inlineData[64];
        void* ptr;
    } storage_;
}
```

### Storage Modes

| Mode | Condition | Allocation | Deallocation |
|------|-----------|------------|--------------|
| `Empty` | Default-constructed or moved-from | None | None |
| `Inline` | `sizeof(T) ≤ 64` AND `alignof(T) ≤ alignof(max_align_t)` AND `nothrow_move_constructible` | Placement-new into inline buffer | Destructor called in-place |
| `Pool` | All other types | `defaultMemoryPool().allocate()` | `allocator_->deallocate()` via vtable |

**Decision logic in `make<T>()`:**

```cpp
if constexpr (sizeof(DT) <= kInlineSize
              && alignof(DT) <= alignof(std::max_align_t)
              && std::is_nothrow_move_constructible_v<DT>) {
    // Inline — zero heap allocation
} else {
    // Pool — slab allocator
}
```

### Type-Erased Construction

```cpp
template<typename T>
static Message make(T&& value) {
    using DT = std::decay_t<T>;
    Message msg;
    msg.id_ = DT::kId;        // Extract static MessageId from payload
    msg.ops_ = opsFor<DT>();  // Get compile-time vtable

    if constexpr (/* inline-eligible */) {
        msg.mode_ = StorageMode::Inline;
        ::new (msg.storage_.inlineData) DT(std::forward<T>(value));
    } else {
        msg.mode_ = StorageMode::Pool;
        void* mem = alloc->allocate(sizeof(DT), alignof(DT));
        msg.storage_.ptr = ::new(mem) DT(std::forward<T>(value));
        msg.allocator_ = alloc;
    }
    return msg;
}
```

**Requirements on every payload type `T`:**
- Must have `static constexpr MessageId kId`
- Must be move-constructible (inline requires `nothrow`)
- Copy-constructibility is optional (enables `clone()`)

### Move Semantics

Move is implemented via the vtable's `move` function pointer, which placement-move-constructs the element into the destination and destroys the source:

```cpp
// Inline move
ops_->move(storage_.inlineData, other.storage_.inlineData);

// Pool move — just transfer the pointer
storage_.ptr = other.storage_.ptr;
```

After move, the source is reset to `Empty`.

### Cloning

```cpp
Message clone() const;
```

| Source Mode | Copyable | Result |
|-------------|----------|--------|
| `Empty` | — | Empty message |
| `Inline` | Yes | `cloneConstruct` — placement-new copy into destination inline buffer |
| `Inline` | No | Empty message (clone not supported) |
| `Pool` | Yes | `cloneAllocate` — allocate from pool + copy-construct |
| `Pool` | No | Empty message (clone not supported) |

Cloning is used primarily by the timer system, which needs to deliver the same message to multiple actors or repeatedly.

---

## MessageOps Vtable

**File:** `src/core/actor_system/messages/message.hpp:132-187`

A compile-time function pointer table that replaces RTTI/virtual dispatch:

```cpp
struct MessageOps {
    void  (*destroy)(void*, IMemoryAllocator*);           // Destructor + deallocation
    void  (*move)(void* dst, void* src);                  // Move-construct + destroy source
    void  (*cloneConstruct)(void* dst, const void* src);  // Copy-construct inline (nullptr if non-copyable)
    void* (*cloneAllocate)(const void* src, IMemoryAllocator* alloc);  // Alloc + copy (nullptr if non-copyable)
};
```

Generated per-type via `opsFor<T>()`:

```cpp
template<typename T>
static const MessageOps* opsFor() {
    static constexpr bool isPool = (sizeof(T) > kInlineSize) || ...;
    static constexpr bool isCopyable = std::is_copy_constructible_v<T>;
    // Returns pointer to static constexpr MessageOps instance
}
```

Each concrete type gets exactly one `static constexpr MessageOps` instance — zero runtime overhead for vtable lookup.

---

## MessageId & Traits

**File:** `src/core/actor_system/messages/message_traits.hpp`

```cpp
enum class MessageId : uint32_t {
    // Core
    SignalNotify = 1,
    // Actor
    ActorEnableRequest, ActorDisableRequest, ActorRestartRequest,
    // Tick
    Tick,
    // Cmd
    CmdRequest, CmdResponse,
    // Ipc
    IpcNewConnection, IpcDataReceived,
    // Monitor
    MonitorNewConnection, MonitorClientDisconnected,
    MonitorSubscribe, MonitorUnsubscribe, MonitorSnapshotUpdate,
    // D-Bus
    DbusRegisterMethod, DbusUnregisterMethod, DbusRegisterResult,
    DbusIncomingMethodCall, DbusMethodCallResult,
    DbusProxyCallRequest, DbusProxyCallResult,
    DbusSubscribeSignal, DbusSignalEvent,
    // Network Manager
    NmStatusRequest,
    // Wi-Fi
    WifiScanRequest, WifiScanResult,
    WifiConnectRequest, WifiConnectResult,
    WifiDisconnectRequest, WifiDisconnectResult,
    WifiStatusResult, WifiAutoReconnectRequest,
    // PMU
    PmuDataTick, PmuDataSubscribe, PmuDataUnsubscribe, PmuDataUpdate,
    // System
    SysDataTick, SysDataSubscribe, SysDataUnsubscribe, SysDataUpdate,
};
```

**37 message types** across 10 subsystems.

**Trait contract** — every message struct must:
1. Declare `static constexpr MessageId kId`
2. Be move-constructible
3. Optionally be copy-constructible (enables `clone()`)

---

## Type-Safe Dispatch

### Visit Pattern

**File:** `src/core/actor_system/messages/message.hpp:99-110`

```cpp
template<typename Tuple, typename Visitor>
bool visit(Visitor&& v) const {
    return std::apply([&](auto... t) {
        auto tryVisit = [&](auto x) {
            using T = std::decay_t<decltype(x)>;
            if (id_ != T::kId) return false;  // Runtime ID check
            v(as<T>());                         // Type-safe downcast
            return true;
        };
        return (tryVisit(t) || ...);            // Fold expression
    }, Tuple{});
}
```

The visit pattern takes a `std::tuple<T1, T2, ...>` listing the types an actor handles, and uses a fold expression to try each type in order. The first match invokes the visitor with the correctly-typed reference.

### Compile-Time Handler Verification

**File:** `src/core/actor_system/actor/actor.hpp:36-48`

```cpp
template<class ActorT, typename Tuple>
void dispatch(ActorT& self, const Message& msg, Tuple) {
    constexpr bool hasAllHandlers = []<std::size_t... I>(std::index_sequence<I...>){
        return (requires(ActorT& a, const std::tuple_element_t<I, Tuple>& m){
            a.handle(m);
        } && ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    static_assert(hasAllHandlers, "dispatch tuple: handle(T) no overload");

    bool matched = msg.visit<Tuple>([&](const auto& m) {
        self.handle(m);
    });
    if (!matched) self.handleUnknown(msg);
}
```

The `static_assert` uses C++20 `requires` expressions to verify **at compile time** that the actor has a `handle(const T&)` overload for every type in the tuple. A missing handler is a build error, not a runtime bug.

**Usage in actors:**

```cpp
// Declare which messages this actor handles
using CmdActorMessages = std::tuple<CmdRequest, PmuDataUpdate, WifiScanResult, ...>;

void CmdActor::handle(const Message& msg) {
    Actor::dispatch(*this, msg, CmdActorMessages{});  // compile-time checked
}

// Typed handlers
void CmdActor::handle(const CmdRequest& m) { ... }
void CmdActor::handle(const PmuDataUpdate& m) { ... }
```

### Dead Letter Handling

Unmatched messages (no `visit` match) fall through to `handleUnknown()`:

```cpp
void Actor::handleUnknown(const Message& msg) {
    V2_LOG_WARN("Actor {}: unhandled message id {}", name_.c_str(), (int)msg.id());
    V2_METRICS()->recordDeadLetter(id());
}
```

---

## Core Messages

**File:** `src/core/actor_system/messages/core_messages.hpp`

System-level messages intercepted by `ActorRuntime::tryConsumeLifecycle()` before reaching the actor's `handle()`:

| Message | kId | Fields | Behavior |
|---------|-----|--------|----------|
| `SignalNotify` | `SignalNotify` | `int signum` | POSIX signal forwarded from event loop |
| `ActorEnableRequest` | `ActorEnableRequest` | (none) | Opens a `Closed` actor |
| `ActorDisableRequest` | `ActorDisableRequest` | (none) | Closes an `Opened` actor (non-essential only) |
| `ActorRestartRequest` | `ActorRestartRequest` | `std::string reason` | Close + reopen (supervisor-driven) |

---

## Message Catalog

### Core Lifecycle

| Message | kId | Fields | Source |
|---------|-----|--------|--------|
| `SignalNotify` | `SignalNotify` | `int signum` | `SystemManagerActor` (event loop → signal pipe) |
| `ActorEnableRequest` | `ActorEnableRequest` | — | `CmdActor`, system |
| `ActorDisableRequest` | `ActorDisableRequest` | — | `CmdActor`, system |
| `ActorRestartRequest` | `ActorRestartRequest` | `std::string reason` | `Supervisor` |

### Tick

| Message | kId | Fields | Source |
|---------|-----|--------|--------|
| `Tick` | `Tick` | — | `TickActor` (periodic timer) |

### Command (Cmd)

| Message | kId | Fields | Source |
|---------|-----|--------|--------|
| `CmdRequest` | `CmdRequest` | `ConnHandle conn`, `std::string cmd` | `IpcServerActor` |
| `CmdResponse` | `CmdResponse` | `ConnHandle conn`, `std::string result`, `bool closeOnSend` | `CmdActor` |

### IPC

| Message | kId | Fields | Source |
|---------|-----|--------|--------|
| `IpcNewConnection` | `IpcNewConnection` | `ConnHandle conn` | `IpcServerActor` (epoll) |
| `IpcDataReceived` | `IpcDataReceived` | `ConnHandle conn` | `IpcServerActor` (epoll) |

### Monitor

| Message | kId | Fields | Source |
|---------|-----|--------|--------|
| `MonitorNewConnection` | `MonitorNewConnection` | `ConnHandle conn` | `MonitorBridgeActor` |
| `MonitorClientDisconnected` | `MonitorClientDisconnected` | `ConnHandle conn` | `MonitorBridgeActor` |
| `MonitorSubscribe` | `MonitorSubscribe` | `std::string subscriber` | `MonitorBridgeActor` |
| `MonitorUnsubscribe` | `MonitorUnsubscribe` | `std::string subscriber` | `MonitorBridgeActor` |
| `MonitorSnapshotUpdate` | `MonitorSnapshotUpdate` | `MonitorSnapshot snapshot` | `MonitorActor` |

### D-Bus

| Message | kId | Fields |
|---------|-----|--------|
| `DbusRegisterMethod` | `DbusRegisterMethod` | `objectPath`, `interfaceName`, `methodName`, `ownerActorName` |
| `DbusUnregisterMethod` | `DbusUnregisterMethod` | `objectPath`, `interfaceName`, `methodName` |
| `DbusRegisterResult` | `DbusRegisterResult` | `methodKey`, `bool success`, `errorMsg` |
| `DbusIncomingMethodCall` | `DbusIncomingMethodCall` | `callId`, `objectPath`, `interfaceName`, `methodName`, `args`, `senderActorName` |
| `DbusMethodCallResult` | `DbusMethodCallResult` | `callId`, `result`, `bool isError` |
| `DbusProxyCallRequest` | `DbusProxyCallRequest` | `callId`, `destination`, `objectPath`, `interfaceName`, `methodName`, `args`, `requesterActorName` |
| `DbusProxyCallResult` | `DbusProxyCallResult` | `callId`, `result`, `bool isError` |
| `DbusSubscribeSignal` | `DbusSubscribeSignal` | `destination`, `objectPath`, `interfaceName`, `signalName`, `subscriberActorName` |
| `DbusSignalEvent` | `DbusSignalEvent` | `destination`, `objectPath`, `interfaceName`, `signalName`, `args` |

### Network Manager

| Message | kId | Fields |
|---------|-----|--------|
| `NmStatusRequest` | `NmStatusRequest` | — |

### Wi-Fi

| Message | kId | Fields |
|---------|-----|--------|
| `WifiScanRequest` | `WifiScanRequest` | — |
| `WifiScanResult` | `WifiScanResult` | `std::vector<WifiApInfo> accessPoints` |
| `WifiConnectRequest` | `WifiConnectRequest` | `std::string ssid`, `std::string password` |
| `WifiConnectResult` | `WifiConnectResult` | `bool result`, `std::string errorMsg` |
| `WifiDisconnectRequest` | `WifiDisconnectRequest` | — |
| `WifiDisconnectResult` | `WifiDisconnectResult` | `bool result` |
| `WifiStatusResult` | `WifiStatusResult` | `connected`, `ssid`, `ipAddress`, `state`, `interfaceName`, `signalStrength`, `autoReconnect` |
| `WifiAutoReconnectRequest` | `WifiAutoReconnectRequest` | `bool enable` |

### Device Manager (PMU)

| Message | kId | Fields | Source |
|---------|-----|--------|--------|
| `PmuDataTick` | `PmuDataTick` | — | `DeviceManagerActor` (timer) |
| `PmuDataSubscribe` | `PmuDataSubscribe` | `std::string subscriber` | `MonitorActor` |
| `PmuDataUnsubscribe` | `PmuDataUnsubscribe` | `std::string subscriber` | `MonitorActor` |
| `PmuDataUpdate` | `PmuDataUpdate` | `PmuData data` | `DeviceManagerActor` |

### System Manager

| Message | kId | Fields | Source |
|---------|-----|--------|--------|
| `SysDataTick` | `SysDataTick` | — | `SystemManagerActor` (timer) |
| `SysDataSubscribe` | `SysDataSubscribe` | `std::string subscriber` | `MonitorActor` |
| `SysDataUnsubscribe` | `SysDataUnsubscribe` | `std::string subscriber` | `MonitorActor` |
| `SysDataUpdate` | `SysDataUpdate` | `SystemResources data` | `SystemManagerActor` |

---

## Communication Patterns

### Request-Response

Asynchronous request followed by a response message sent back to the requester:

```
CmdActor                    NetworkManagerActor
    │                              │
    ├── WifiScanRequest ──────────>│
    │                              │ (scan Wi-Fi)
    │<──── WifiScanResult ─────────┤
    │                              │
```

The response is sent by name — no callback or future needed. The requesting actor identifies itself via fields in the request message (e.g., `ConnHandle conn`).

### Pub-Sub (Retained-Latest)

Subscription-based data delivery where the latest value is retained:

```
MonitorActor          SystemManagerActor
    │                      │
    ├── SysDataSubscribe ─>│  (register subscriber)
    │                      │
    │<── SysDataUpdate ────┤  (periodic push)
    │<── SysDataUpdate ────┤
    │                      │
    ├── SysDataUnsubscribe>│  (deregister)
```

Subscribers are identified by actor name. The producer pushes updates to all registered subscribers. Data collection starts when the first subscriber connects and stops when the last disconnects (demand cascade).

### Timer-Driven Periodic

Actor registers a timer that periodically enqueues a message into its own mailbox:

```cpp
void TickActor::open() {
    startTimer(Tick{}, tickMs_, true);  // repeating
}

void TickActor::handle(const Tick& t) {
    // periodic work
}
```

### Delayed Message Delivery

Send a message to another actor after a specified delay:

```cpp
sendMsgAfter("target_actor", MyMessage{...}, 5000);  // 5 seconds delay
```

Uses the `Scheduler` to clone the message and enqueue it into the target's mailbox after the delay.

### Lifecycle Messages (System-Intercepted)

`ActorEnableRequest`, `ActorDisableRequest`, and `ActorRestartRequest` are consumed by `ActorRuntime::tryConsumeLifecycle()` before reaching the actor:

```cpp
bool ActorRuntime::tryConsumeLifecycle(const Message& msg) {
    switch(msg.id()) {
        case MessageId::ActorEnableRequest:
            if(actor_->getState() == Closed) actor_->open();
            return true;
        case MessageId::ActorDisableRequest:
            if(actor_->getState() == Opened && !actor_->isEssential()) actor_->close();
            return true;
        case MessageId::ActorRestartRequest:
            if(actor_->getState() == Opened) performRestart(msg.as<ActorRestartRequest>().reason);
            return true;
        default:
            return false;
    }
}
```

### Event Loop Bridge

External I/O events (POSIX signals, socket data) are injected as messages into actor mailboxes via the event loop:

```cpp
// SystemManagerActor subscribes to signal pipe fd
eventLoop_->subscribe(signalFd, [this]() {
    int sig = readSignal();
    receiveMsg(SignalNotify{sig});  // inject into mailbox
});
```

---

## Message Flow (End-to-End)

```
1. Creation     Message::make(payload)          — type-erased, inline/pool decision
       ↓
2. Sending      actor->sendMsg("target", msg)   — template wraps Message::make()
       ↓
3. Routing      Registry lookup by name → Actor*
       ↓
4. Enqueue      target->receiveMsg(msg)         — runtime_->enqueue(msg)
       ↓
5. Mailbox      MPSC lock-free push             — non-blocking, drop on full
       ↓
6. Dispatch     scheduled_ gate → WorkDispatcher — one-shot dispatch per actor
       ↓
7. Acquire      Worker pops ActorRuntime*       — semaphore wake or steal
       ↓
8. Batch        ActorRuntime::run(maxBatch)     — pop ≤32 messages
       ↓
9. Lifecycle    tryConsumeLifecycle()            — intercept system messages
       ↓
10. Handle      actor->handle(msg)              — dispatch() → visit → typed handler
       ↓
11. Dead Letter handleUnknown()                  — unmatched → log + metrics
```

---

## Summary

| Aspect | Detail |
|--------|--------|
| **Container** | `Message` — 72 bytes, type-erased, move-only |
| **SBO threshold** | 64 bytes inline (zero heap for most messages) |
| **Vtable** | `MessageOps` — compile-time generated, per-type `static constexpr` |
| **Discriminator** | `MessageId` enum (`uint32_t`) — 37 types across 10 subsystems |
| **Dispatch** | `visit<Tuple>()` fold expression + `static_assert` handler verification |
| **Clone** | Explicit `clone()` — inline copy or pool alloc, disabled for non-copyable types |
| **Patterns** | Request-response, pub-sub, timer-driven, delayed, lifecycle-intercepted, event-loop bridge |
| **Throughput** | 7.1M msgs/sec (SBO + lock-free mailbox) |
| **Latency** | P50 = 378 ns, P99 = 641 ns |
