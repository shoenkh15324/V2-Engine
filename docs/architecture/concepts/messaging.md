# 메시지 시스템

액터들이 주고받는 `Message`의 내부 구조, 40개 메시지 카탈로그, 실전 통신 패턴을 처음 읽는 사람도 따라올 수 있게 정리한 문서.

---

## 목차

- [요약 (세 문장)](#요약-세-문장)
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

---

## 요약 (세 문장)

1. 액터들이 주고받는 모든 데이터는 96바이트짜리 **`Message` 상자 하나**로 포장됩니다 — 큐가 여러 타입을 담을 수 있도록 타입을 감추는(타입 소거) 기법입니다.
2. 64바이트 이하 payload는 상자 안에 직접 들어가 힙 할당이 없습니다(SBO) — 이것이 초당 수백만 메시지의 원천입니다.
3. 메시지는 복사되지 않고 **이동**합니다; 개발자는 `handle(const T&)` 오버로드만 만들면 되고, 누락은 컴파일 에러로 잡아줍니다.

---

## 개요

V² Engine은 액터 간 범용 통신 메커니즘으로 **타입 소거·SBO 최적화·이동 전용** 메시지 시스템을 사용합니다. CLI 명령 처리부터 타이머 기반 하트비트까지 모든 상호작용은 유니폼 `Message` 컨테이너로 래핑된 타입화된 메시지 구조체를 통해 흐릅니다.

이 다소 낯선 조합("타입을 감춘다?")은 사실 세 가지 현실적인 문제에 대한 답입니다:

| 문제 | 해법 |
|------|------|
| 메일박스(MPSC 큐)는 **한 가지 타입만** 담을 수 있는데, 메시지 종류는 40개 | 모든 payload를 동일한 `Message` 상자에 포장 (**타입 소거**) — 큐는 `Message`만 보면 됨 |
| 메시지마다 `new`로 힙 할당하면 잦은 malloc이 병목이자 잠금 경쟁의 온상 | 64B 이하는 상자 안에 직접 저장 (**SBO**), 큰 것만 [메모리 풀](memory.md) 사용 |
| 대부분의 메시지는 만들어서 전달하고 버리는데, 복사 비용이 아깝다 | **이동 전용** 설계 — 정말 필요할 때만 명시적 `clone()` |

각 해법이 문서 뒤쪽 어디에서 구현되는지 순서대로 따라가 보면 됩니다.

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
Message (96 bytes @ Linux x86-64/aarch64, alignof = 16) {
    MessageId          id_;        // offset  0 · 4 B  — type discriminator
    StorageMode        mode_;      // offset  4 · 1 B  — Empty | Inline | Pool
    /* padding 3 B */              // offset 5..8      — ops_ 포인터 정렬용
    const MessageOps*  ops_;       // offset  8 · 8 B  — vtable pointer
    IMemoryAllocator*  allocator_; // offset 16 · 8 B  — pool allocator (for Pool mode)
    /* padding 8 B */              // offset 24..32    — storage_의 16바이트 정렬용
    union {                        // offset 32 · 64 B — inline buffer or heap pointer
        alignas(max_align_t) std::byte inlineData[64];
        void* ptr;
    } storage_;
}
```

왜 필드 합계(85B)보다 클까? **패딩이 두 군데** 있기 때문입니다. `ops_` 앞 3바이트는 포인터 정렬용이고, `storage_` 앞 8바이트는 `alignas(std::max_align_t)`(리눅스 기준 16) 때문입니다. 인라인 버퍼에 어떤 타입이든 placement-new 하려면 최대 정렬 요건을 만족해야 하므로, 이것은 의도된 비용입니다. (참고: `max_align_t`가 8인 MSVC 등에서는 88바이트가 되지만, 이 프로젝트의 타깃은 리눅스입니다.)

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

## MessageId & Traits

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

11개 카테고리에 걸친 **40개 메시지 타입**.

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

> 📖 **폴드 표현식**(fold expression): C++17 문법으로, 가변 인자 템플릿(파라미터 팩)의 요소들에 `(패턴 && ...)` 같은 형태로 연산자를 접어 적용하는 것. 여기서는 튜플의 모든 타입 T에 대해 `tryVisit(T)`를 순차 실행하고 하나라도 성공하면 true를 내놓는 데 쓰입니다.

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
