# 메시지 시스템

---

## 목차

- [개요](#개요)
- [설계 원칙](#설계-원칙)
- [Message 클래스](#message-클래스)
  - [저장 레이아웃](#저장-레이아웃)
  - [저장 모드](#저장-모드)
  - [타입 소거 생성](#타입-소거-생성)
  - [이동 시맨틱스](#이동-시맨틱스)
  - [복제](#복제)
- [MessageOps Vtable](#messageops-vtable)
- [MessageId & 트레이트](#messageid--트레이트)
- [타입 안전 디스패치](#타입-안전-디스패치)
  - [방문 패턴](#방문-패턴)
  - [컴파일 타임 핸들러 검증](#컴파일-타임-핸들러-검증)
  - [데드 레터 처리](#데드-레터-처리)
- [핵심 메시지](#핵심-메시지)
- [메시지 카탈로그](#메시지-카탈로그)
  - [핵심 생명주기](#핵심-생명주기)
  - [Tick](#tick)
  - [Command (Cmd)](#command-cmd)
  - [IPC](#ipc)
  - [Monitor](#monitor)
  - [D-Bus](#d-bus)
  - [Network Manager](#network-manager)
  - [Wi-Fi](#wi-fi)
  - [Device Manager (PMU)](#device-manager-pmu)
  - [System Manager](#system-manager)
- [통신 패턴](#통신-패턴)
  - [요청-응답](#요청-응답)
  - [Pub-Sub (최신 유지)](#pub-sub-최신-유지)
  - [타이머 기반 주기](#타이머-기반-주기)
  - [지연 메시지 전달](#지연-메시지-전달)
  - [생명주기 메시지 (시스템 가로채기)](#생명주기-메시지-시스템-가로채기)
  - [이벤트 루프 브릿지](#이벤트-루프-브릿지)
- [메시지 흐름 (End-to-End)](#메시지-흐름-end-to-end)
- [요약](#요약)

---

## 개요

V² Engine은 액터 간 범용 통신 메커니즘으로 **타입 소거·SBO 최적화·이동 전용** 메시지 시스템을 사용합니다. CLI 명령 처리부터 타이머 기반 하트비트까지 모든 상호작용은 유니폼 `Message` 컨테이너로 래핑된 타입화된 메시지 구조체를 통해 흐릅니다.

이 시스템은 **핫 패스에서 힙 할당 제로**를 목표로 설계되었습니다: 64바이트 이하의 메시지는 `Message` 객체 자체에 인라인 저장됩니다. 락프리 MPSC 메일박스와 결합하여 **7.1M msgs/sec** 쓰루풋과 **P50 = 378 ns** 레이턴시를 달성합니다.

---

## 설계 원칙

| 원칙 | 설명 |
|------|------|
| **제로 오버헤드 타입 소거** | `Message`는 가상 디스패치 없이 어떤 페이로드 타입이든 래핑합니다. 컴파일 타임 vtable(`MessageOps`)이 RTTI를 대체합니다. |
| **Small Buffer Optimization (SBO)** | 64바이트 이하 메시지는 인라인 저장 — 힙 할당 없음. 시스템 내 대부분의 메시지 타입을 커버합니다. |
| **이동 전용** | 메시지는 복사되지 않고 이동됩니다. `clone()`은 중복이 필요한 타이머 콜백을 위해 명시적으로 제공됩니다. |
| **컴파일 타임 안전성** | `dispatch()`는 C++20 `requires` 표현식을 사용하여 모든 메시지 타입에 핸들러가 있는지 빌드 타임에 검증합니다. |
| **유니폼 판별자** | 모든 페이로드 타입은 `static constexpr MessageId kId`를 선언합니다 — 런타임 라우팅에 사용되는 단일 `uint32_t` 열거형입니다. |

---

## Message 클래스

**파일:** `src/core/actor_system/messages/message.hpp`

### 저장 레이아웃

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

### 저장 모드

| 모드 | 조건 | 할당 | 해제 |
|------|------|------|------|
| `Empty` | 기본 생성 또는 이동된 후 | 없음 | 없음 |
| `Inline` | `sizeof(T) ≤ 64` AND `alignof(T) ≤ alignof(max_align_t)` AND `nothrow_move_constructible` | 인라인 버퍼에 placement-new | 소멸자 직접 호출 |
| `Pool` | 그 외 모든 타입 | `defaultMemoryPool().allocate()` | vtable 경유 `allocator_->deallocate()` |

**`make<T>()`의 판정 로직:**

```cpp
if constexpr (sizeof(DT) <= kInlineSize
              && alignof(DT) <= alignof(std::max_align_t)
              && std::is_nothrow_move_constructible_v<DT>) {
    // Inline — zero heap allocation
} else {
    // Pool — slab allocator
}
```

### 타입 소거 생성

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

**모든 페이로드 타입 `T`에 대한 요구사항:**
- `static constexpr MessageId kId`가 있어야 합니다
- 이동 생성 가능해야 합니다 (인라인은 `nothrow` 필요)
- 복사 생성은 선택적입니다 (`clone()` 활성화)

### 이동 시맨틱스

이동은 vtable의 `move` 함수 포인터를 통해 구현되며, 대상에 요소를 placement-move-construct하고 소스를 파괴합니다:

```cpp
// Inline move
ops_->move(storage_.inlineData, other.storage_.inlineData);

// Pool move — just transfer the pointer
storage_.ptr = other.storage_.ptr;
```

이동 후 소스는 `Empty`로 리셋됩니다.

### 복제

```cpp
Message clone() const;
```

| 소스 모드 | 복사 가능 | 결과 |
|-----------|-----------|------|
| `Empty` | — | 빈 메시지 |
| `Inline` | 예 | `cloneConstruct` — 대상 인라인 버퍼에 placement-new 복사 |
| `Inline` | 아니오 | 빈 메시지 (복제 불가) |
| `Pool` | 예 | `cloneAllocate` — 풀에서 할당 + 복사 생성 |
| `Pool` | 아니오 | 빈 메시지 (복제 불가) |

복제는 주로 동일한 메시지를 여러 액터에 전달하거나 반복적으로 전달해야 하는 타이머 시스템에서 사용됩니다.

---

## MessageOps Vtable

**파일:** `src/core/actor_system/messages/message.hpp:132-187`

RTTI/가상 디스패치를 대체하는 컴파일 타임 함수 포인터 테이블:

```cpp
struct MessageOps {
    void  (*destroy)(void*, IMemoryAllocator*);           // Destructor + deallocation
    void  (*move)(void* dst, void* src);                  // Move-construct + destroy source
    void  (*cloneConstruct)(void* dst, const void* src);  // Copy-construct inline (nullptr if non-copyable)
    void* (*cloneAllocate)(const void* src, IMemoryAllocator* alloc);  // Alloc + copy (nullptr if non-copyable)
};
```

`opsFor<T>()`를 통해 타입별로 생성됩니다:

```cpp
template<typename T>
static const MessageOps* opsFor() {
    static constexpr bool isPool = (sizeof(T) > kInlineSize) || ...;
    static constexpr bool isCopyable = std::is_copy_constructible_v<T>;
    // Returns pointer to static constexpr MessageOps instance
}
```

각 구체 타입은 정확히 하나의 `static constexpr MessageOps` 인스턴스를 갖습니다 — vtable 조회에 런타임 오버헤드 제로.

---

## MessageId & 트레이트

**파일:** `src/core/actor_system/messages/message_traits.hpp`

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

10개 서브시스템에 걸친 **37개 메시지 타입**.

**트레이트 계약** — 모든 메시지 구조체는 다음을 만족해야 합니다:
1. `static constexpr MessageId kId` 선언
2. 이동 생성 가능
3. 선택적으로 복사 생성 가능 (`clone()` 활성화)

---

## 타입 안전 디스패치

### 방문 패턴

**파일:** `src/core/actor_system/messages/message.hpp:99-110`

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

방문 패턴은 액터가 처리하는 타입을 나열하는 `std::tuple<T1, T2, ...>`을 인자로 받아, 폴드 표현식을 사용하여 각 타입을 순서대로 시도합니다. 첫 번째 일치가 올바르게 타입화된 참조로 방문자를 호출합니다.

### 컴파일 타임 핸들러 검증

**파일:** `src/core/actor_system/actor/actor.hpp:36-48`

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

`static_assert`는 C++20 `requires` 표현식을 사용하여 **컴파일 타임에** 해당 액터가 튜플의 모든 타입에 대한 `handle(const T&)` 오버로드를 가지고 있음을 검증합니다. 핸들러 누락은 런타임 버그가 아닌 빌드 에러입니다.

**액터에서의 사용:**

```cpp
// 이 액터가 처리할 메시지 선언
using CmdActorMessages = std::tuple<CmdRequest, PmuDataUpdate, WifiScanResult, ...>;

void CmdActor::handle(const Message& msg) {
    Actor::dispatch(*this, msg, CmdActorMessages{});  // compile-time checked
}

// 타입화된 핸들러
void CmdActor::handle(const CmdRequest& m) { ... }
void CmdActor::handle(const PmuDataUpdate& m) { ... }
```

### 데드 레터 처리

일치하지 않는 메시지(`visit` 미매칭)는 `handleUnknown()`으로 전달됩니다:

```cpp
void Actor::handleUnknown(const Message& msg) {
    V2_LOG_WARN("Actor {}: unhandled message id {}", name_.c_str(), (int)msg.id());
    V2_METRICS()->recordDeadLetter(id());
}
```

---

## 핵심 메시지

**파일:** `src/core/actor_system/messages/core_messages.hpp`

`ActorRuntime::tryConsumeLifecycle()`에 의해 액터의 `handle()`에 도달하기 전에 가로채지는 시스템 수준 메시지:

| 메시지 | kId | 필드 | 동작 |
|--------|-----|------|------|
| `SignalNotify` | `SignalNotify` | `int signum` | 이벤트 루프에서 전달된 POSIX 시그널 |
| `ActorEnableRequest` | `ActorEnableRequest` | (없음) | `Closed` 액터를 개시 |
| `ActorDisableRequest` | `ActorDisableRequest` | (없음) | `Opened` 액터를 종료 (essential 아닌 경우만) |
| `ActorRestartRequest` | `ActorRestartRequest` | `std::string reason` | 종료 + 재개시 (슈퍼바이저 주도) |

---

## 메시지 카탈로그

### 핵심 생명주기

| 메시지 | kId | 필드 | 소스 |
|--------|-----|------|------|
| `SignalNotify` | `SignalNotify` | `int signum` | `SystemManagerActor` (event loop → signal pipe) |
| `ActorEnableRequest` | `ActorEnableRequest` | — | `CmdActor`, 시스템 |
| `ActorDisableRequest` | `ActorDisableRequest` | — | `CmdActor`, 시스템 |
| `ActorRestartRequest` | `ActorRestartRequest` | `std::string reason` | `Supervisor` |

### Tick

| 메시지 | kId | 필드 | 소스 |
|--------|-----|------|------|
| `Tick` | `Tick` | — | `TickActor` (주기 타이머) |

### Command (Cmd)

| 메시지 | kId | 필드 | 소스 |
|--------|-----|------|------|
| `CmdRequest` | `CmdRequest` | `ConnHandle conn`, `std::string cmd` | `IpcServerActor` |
| `CmdResponse` | `CmdResponse` | `ConnHandle conn`, `std::string result`, `bool closeOnSend` | `CmdActor` |

### IPC

| 메시지 | kId | 필드 | 소스 |
|--------|-----|------|------|
| `IpcNewConnection` | `IpcNewConnection` | `ConnHandle conn` | `IpcServerActor` (epoll) |
| `IpcDataReceived` | `IpcDataReceived` | `ConnHandle conn` | `IpcServerActor` (epoll) |

### Monitor

| 메시지 | kId | 필드 | 소스 |
|--------|-----|------|------|
| `MonitorNewConnection` | `MonitorNewConnection` | `ConnHandle conn` | `MonitorBridgeActor` |
| `MonitorClientDisconnected` | `MonitorClientDisconnected` | `ConnHandle conn` | `MonitorBridgeActor` |
| `MonitorSubscribe` | `MonitorSubscribe` | `std::string subscriber` | `MonitorBridgeActor` |
| `MonitorUnsubscribe` | `MonitorUnsubscribe` | `std::string subscriber` | `MonitorBridgeActor` |
| `MonitorSnapshotUpdate` | `MonitorSnapshotUpdate` | `MonitorSnapshot snapshot` | `MonitorActor` |

### D-Bus

| 메시지 | kId | 필드 |
|--------|-----|------|
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

| 메시지 | kId | 필드 |
|--------|-----|------|
| `NmStatusRequest` | `NmStatusRequest` | — |

### Wi-Fi

| 메시지 | kId | 필드 |
|--------|-----|------|
| `WifiScanRequest` | `WifiScanRequest` | — |
| `WifiScanResult` | `WifiScanResult` | `std::vector<WifiApInfo> accessPoints` |
| `WifiConnectRequest` | `WifiConnectRequest` | `std::string ssid`, `std::string password` |
| `WifiConnectResult` | `WifiConnectResult` | `bool result`, `std::string errorMsg` |
| `WifiDisconnectRequest` | `WifiDisconnectRequest` | — |
| `WifiDisconnectResult` | `WifiDisconnectResult` | `bool result` |
| `WifiStatusResult` | `WifiStatusResult` | `connected`, `ssid`, `ipAddress`, `state`, `interfaceName`, `signalStrength`, `autoReconnect` |
| `WifiAutoReconnectRequest` | `WifiAutoReconnectRequest` | `bool enable` |

### Device Manager (PMU)

| 메시지 | kId | 필드 | 소스 |
|--------|-----|------|------|
| `PmuDataTick` | `PmuDataTick` | — | `DeviceManagerActor` (timer) |
| `PmuDataSubscribe` | `PmuDataSubscribe` | `std::string subscriber` | `MonitorActor` |
| `PmuDataUnsubscribe` | `PmuDataUnsubscribe` | `std::string subscriber` | `MonitorActor` |
| `PmuDataUpdate` | `PmuDataUpdate` | `PmuData data` | `DeviceManagerActor` |

### System Manager

| 메시지 | kId | 필드 | 소스 |
|--------|-----|------|------|
| `SysDataTick` | `SysDataTick` | — | `SystemManagerActor` (timer) |
| `SysDataSubscribe` | `SysDataSubscribe` | `std::string subscriber` | `MonitorActor` |
| `SysDataUnsubscribe` | `SysDataUnsubscribe` | `std::string subscriber` | `MonitorActor` |
| `SysDataUpdate` | `SysDataUpdate` | `SystemResources data` | `SystemManagerActor` |

---

## 통신 패턴

### 요청-응답

요청자에게 응답 메시지를 비동기적으로 전송하는 패턴:

```
CmdActor                    NetworkManagerActor
    │                              │
    ├── WifiScanRequest ──────────>│
    │                              │ (scan Wi-Fi)
    │<──── WifiScanResult ─────────┤
    │                              │
```

응답은 이름으로 전송됩니다 — 콜백이나 퓨처가 필요 없습니다. 요청 액터는 요청 메시지의 필드를 통해 자신을 식별합니다 (예: `ConnHandle conn`).

### Pub-Sub (최신 유지)

최신 값을 유지하는 구독 기반 데이터 전달:

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

구독자는 액터 이름으로 식별됩니다. 프로듀서는 등록된 모든 구독자에게 업데이트를 푸시합니다. 데이터 수집은 첫 번째 구독자가 연결될 때 시작되고 마지막 구독자가 연결 해제될 때 중지됩니다 (수요 캐스케이드).

### 타이머 기반 주기

액터가 자체 메일박스에 주기적으로 메시지를 큐잉하는 타이머를 등록합니다:

```cpp
void TickActor::open() {
    startTimer(Tick{}, tickMs_, true);  // repeating
}

void TickActor::handle(const Tick& t) {
    // periodic work
}
```

### 지연 메시지 전달

지정된 지연 후 다른 액터에게 메시지를 전송합니다:

```cpp
sendMsgAfter("target_actor", MyMessage{...}, 5000);  // 5 seconds delay
```

`Scheduler`를 사용하여 메시지를 복제하고 지연 후 대상의 메일박스에 큐잉합니다.

### 생명주기 메시지 (시스템 가로채기)

`ActorEnableRequest`, `ActorDisableRequest`, `ActorRestartRequest`는 액터에 도달하기 전에 `ActorRuntime::tryConsumeLifecycle()`에 의해 소비됩니다:

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

### 이벤트 루프 브릿지

외부 I/O 이벤트(POSIX 시그널, 소켓 데이터)는 이벤트 루프를 통해 액터 메일박스에 메시지로 주입됩니다:

```cpp
// SystemManagerActor subscribes to signal pipe fd
eventLoop_->subscribe(signalFd, [this]() {
    int sig = readSignal();
    receiveMsg(SignalNotify{sig});  // inject into mailbox
});
```

---

## 메시지 흐름 (End-to-End)

```
1. 생성        Message::make(payload)          — 타입 소거, 인라인/풀 판정
       ↓
2. 전송        actor->sendMsg("target", msg)   — 템플릿이 Message::make() 래핑
       ↓
3. 라우팅      이름으로 레지스트리 조회 → Actor*
       ↓
4. 큐잉        target->receiveMsg(msg)         — runtime_->enqueue(msg)
       ↓
5. 메일박스    MPSC 락프리 push                — 논블로킹, 가득 차면 드롭
       ↓
6. 디스패치    inFlight 슬롯 게이트 → WorkDispatcher — 액터당 토큰 1개 (dedup 내장)
       ↓
7. 획득        워커가 ActorRuntime* 팝         — 세마포어 웨이크 또는 스틸
       ↓
8. 배치        ActorRuntime::run(maxBatch)     — ≤32개 메시지 팝
       ↓
9. 생명주기    tryConsumeLifecycle()            — 시스템 메시지 가로채기
       ↓
10. 핸들링     actor->handle(msg)              — dispatch() → visit → 타입화된 핸들러
       ↓
11. 데드 레터  handleUnknown()                  — 미매칭 → 로그 + 메트릭
```

---

## 요약

| 항목 | 설명 |
|------|------|
| **컨테이너** | `Message` — 72바이트, 타입 소거, 이동 전용 |
| **SBO 임계값** | 64바이트 인라인 (대부분 메시지에 힙 할당 제로) |
| **Vtable** | `MessageOps` — 컴파일 타임 생성, 타입별 `static constexpr` |
| **판별자** | `MessageId` 열거형 (`uint32_t`) — 10개 서브시스템에 37개 타입 |
| **디스패치** | `visit<Tuple>()` 폴드 표현식 + `static_assert` 핸들러 검증 |
| **복제** | 명시적 `clone()` — 인라인 복사 또는 풀 할당, 복사 불가 타입은 비활성화 |
| **패턴** | 요청-응답, pub-sub, 타이머 기반, 지연, 생명주기 가로채기, 이벤트 루프 브릿지 |
| **쓰루풋** | 7.1M msgs/sec (SBO + 락프리 메일박스) |
| **레이턴시** | P50 = 378 ns, P99 = 641 ns |
