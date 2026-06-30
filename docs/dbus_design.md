# DBUS 확장 설계: 메시지 기반 메서드 등록 및 라우팅

## 1. 목표

- **DbusActor가 등록 소유권**을 가지고, 외부 액터는 메시지를 통해 간접 등록
- 들어오는 DBUS 호출을 **Actor 메시지로 라우팅**하여 메일박스 큐잉/ordering/backpressure 활용
- **sdbus-c++ Async Reply** 패턴으로 논블로킹 응답
- 외부 액터가 **메시지로 프록시 호출**을 요청하고 응답 수신

---

## 2. 현재 구조의 문제

| 문제 | 설명 |
|---|---|
| 메시지 시스템 미연동 | DBUS 이벤트가 sdbus 콜백으로만 처리되고 `handle(Message)`를 타지 않음 |
| 외부 액터 등록 불가 | `MainApp`만 DbusActor 포인터를 보유, 다른 액터는 메서드 등록 수단 없음 |
| 동적 등록 없음 | `open()`에서만 등록 가능, 런타임 등록/해제 불가 |
| 프록시 생명주기 | `getProxy()`가 참조 반환, `close()`시 댕글링 위험 |

---

## 3. 변경 파일

| 파일 | 변경 내용 |
|---|---|
| `src/core/actor_system/messages/dbus_messages.hpp` | 신규 메시지 타입 7개 정의 |
| `src/core/actor_system/messages/message.hpp` | `Message` variant에 DBUS 타입 추가 |
| `src/service/dbus/dbus_actor.hpp` | 새 멤버/메서드 선언 |
| `src/service/dbus/dbus_actor.cpp` | `handle()`, 등록/라우팅/프록시 구현 |

---

## 4. 메시지 타입 (`dbus_messages.hpp`)

```cpp
#pragma once
#include <string>
#include <cstdint>
#include "core/common/util/platform_config.h"

// (1) 외부 액터 → DbusActor: DBUS 메서드 등록 요청
struct DbusRegisterMethod {
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string ownerActorName;   // 이 메서드를 처리할 액터 이름
};

// (2) 외부 액터 → DbusActor: DBUS 메서드 등록 해제
struct DbusUnregisterMethod {
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
};

// (3) DbusActor → owner 액터: DBUS 메서드 호출이 들어왔음
struct DbusMethodInvocation {
    uint64_t callId;
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string args;              // 직렬화된 인자 (V1: string)
    std::string senderActorName;   // DbusActor 자신의 이름 (응답 전송용)
};

// (4) owner 액터 → DbusActor: 메서드 호출 결과
struct DbusMethodCallResult {
    uint64_t callId;
    std::string result;            // 직렬화된 결과 (V1: string)
    bool isError{false};
};

// (5) 외부 액터 → DbusActor: 프록시 호출 요청
struct DbusProxyCallRequest {
    uint64_t callId;
    std::string destination;
    std::string objectPath;
    std::string interfaceName;
    std::string methodName;
    std::string args;
    std::string requesterActorName;
};

// (6) DbusActor → 외부 액터: 프록시 호출 결과
struct DbusProxyCallResult {
    uint64_t callId;
    std::string result;
    bool isError{false};
};

// (7) DbusActor → 외부 액터: 등록 결과 통지
struct DbusRegisterResult {
    std::string methodKey;  // "objectPath:interfaceName:methodName"
    bool success;
    std::string errorMsg;
};
```

---

## 5. Message variant (`message.hpp`)

```cpp
using Message = std::variant<
    Tick,
    IpcNewConnection,
    IpcDataReceived,
    MonitorPoll,
    MonitorNewConnection,
    MonitorClientDisconnected,
    // DBUS
    DbusRegisterMethod,
    DbusUnregisterMethod,
    DbusMethodInvocation,
    DbusMethodCallResult,
    DbusProxyCallRequest,
    DbusProxyCallResult,
    DbusRegisterResult
>;
```

---

## 6. DbusActor 변경

### 6.1 새 private 타입/멤버 (`dbus_actor.hpp`)

```cpp
struct MethodEntry {
    std::string ownerActorName;
    std::string objectPath;
    std::string interfaceName;
};

struct PendingCallEntry {
    sdbus::MethodCall call;
    std::string ownerActorName;
};

// key = "objectPath:interfaceName:methodName"
std::unordered_map<std::string, MethodEntry> registrations_;
std::unordered_map<uint64_t, PendingCallEntry> pendingCalls_;
std::mutex pendingCallsMutex_;
std::atomic<uint64_t> nextCallId_{1};
```

### 6.2 handle()

```cpp
void handle(const Message& msg) override {
    if(state_ < Opened) { V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [this](const DbusRegisterMethod& m){ handleRegisterMethod(m); },
        [this](const DbusUnregisterMethod& m){ handleUnregisterMethod(m); },
        [this](const DbusMethodCallResult& m){ handleMethodCallResult(m); },
        [this](const DbusProxyCallRequest& m){ handleProxyCallRequest(m); },
        [](const auto&){}
    }, msg);
}
```

---

## 7. 핵심 흐름

### 7.1 메서드 등록

```
[ExternalActor] --sendMsg("dbus_actor", DbusRegisterMethod{...})--> [DbusActor]
                                                                          |
                                                                   handle()
                                                                   1. registrations_에 추가
                                                                   2. sdbus::registerMethod() 등록
                                                                      callback = DbusActor의 forwarding 함수
                                                                   3. (선택) DbusRegisterResult 전송
```

```cpp
void handleRegisterMethod(const DbusRegisterMethod& msg) {
    auto key = msg.objectPath + ":" + msg.interfaceName + ":" + msg.methodName;
    if (registrations_.count(key)) {
        sendMsg(msg.ownerActorName,
            DbusRegisterResult{key, false, "Already registered"});
        return;
    }

    registrations_[key] = {msg.ownerActorName, msg.objectPath, msg.interfaceName};

    addMethod(msg.objectPath, msg.interfaceName, msg.methodName,
        [this, owner = msg.ownerActorName,
         objectPath = msg.objectPath,
         interfaceName = msg.interfaceName,
         methodName = msg.methodName](sdbus::MethodCall call) {
            try {
                std::string args;
                try { call >> args; } catch (...) { args = ""; }

                uint64_t callId = nextCallId_++;
                {
                    std::lock_guard<std::mutex> lock(pendingCallsMutex_);
                    pendingCalls_[callId] = {std::move(call), owner};
                }

                sendMsg(owner, DbusMethodInvocation{
                    callId, objectPath, interfaceName,
                    methodName, args, name()
                });
            } catch (const std::exception& e) {
                V2_LOG_ERROR("DbusActor: callback failed - %s", e.what());
            }
        });

    sendMsg(msg.ownerActorName,
        DbusRegisterResult{key, true, ""});
}
```

### 7.2 Incoming DBUS call → Actor 메시지 → 응답

```
[D-Bus Client] ---Ping("hello")--> [sdbus-c++]
                                         |
                                    DbusActor callback (main thread)
                                         |
                                    1. args 추출 ("hello")
                                    2. callId 발급, pendingCalls_에 MethodCall 저장
                                         |
                                    3. sendMsg(owner, DbusMethodInvocation{...})
                                         |
                                    [OwnerActor]
                                         |
                                    handle()에서 DbusMethodInvocation 수신
                                    로직 처리:
                                         |
                                    4. sendMsg("dbus_actor",
                                         DbusMethodCallResult{callId, "Pong: hello"})
                                         |
                                    [DbusActor] (worker thread)
                                         |
                                    handleMethodCallResult():
                                    1. pendingCalls_에서 callId 조회
                                    2. storedCall.createReply()로 응답 생성
                                    3. reply << result; reply.send();
                                    4. pendingCalls_에서 제거
                                         |
                                    [sdbus-c++] ---reply--> [D-Bus Client]
```

```cpp
void handleMethodCallResult(const DbusMethodCallResult& msg) {
    std::lock_guard<std::mutex> lock(pendingCallsMutex_);
    auto it = pendingCalls_.find(msg.callId);
    if (it == pendingCalls_.end()) {
        V2_LOG_ERROR("No pending call for callId: %lu", msg.callId);
        return;
    }

    auto& [storedCall, ownerName] = it->second;
    try {
        auto reply = storedCall.createReply();
        if (msg.isError) {
            reply.createErrorResponse(msg.result);
        } else {
            reply << msg.result;
        }
        reply.send();
    } catch (const std::exception& e) {
        V2_LOG_ERROR("DbusActor: failed to send reply - %s", e.what());
    }
    pendingCalls_.erase(it);
}
```

### 7.3 프록시 호출 (외부 액터 → Remote DBUS Service)

```
[ExternalActor] --sendMsg("dbus_actor", DbusProxyCallRequest{...})--> [DbusActor]
                                                                          |
                                                                    handle():
                                                                    1. getProxy()로 프록시 획득
                                                                    2. proxy.callMethod(...) 동기 호출
                                                                    3. sendMsg(requester, DbusProxyCallResult{...})
```

```cpp
void handleProxyCallRequest(const DbusProxyCallRequest& msg) {
    try {
        auto& proxy = getProxy(msg.destination, msg.objectPath);
        std::string result;
        proxy.callMethod(msg.methodName)
            .onInterface(msg.interfaceName)
            .withArguments(msg.args)
            .storeResultsTo(result);
        sendMsg(msg.requesterActorName,
            DbusProxyCallResult{msg.callId, result, false});
    } catch (const std::exception& e) {
        V2_LOG_ERROR("DbusActor: proxy call failed - %s", e.what());
        sendMsg(msg.requesterActorName,
            DbusProxyCallResult{msg.callId, e.what(), true});
    }
}
```

---

## 8. 스레드 안전성

| 공유 자원 | 접근 위치 | 보호 방식 |
|---|---|---|
| `pendingCalls_` | 메인 스레드 (sdbus callback) & 워커 스레드 (handle) | `std::mutex` |
| `nextCallId_` | 메인 스레드 (sdbus callback) | `std::atomic` |
| `registrations_` | 워커 스레드 (handle() 단일 접근) | 단일 워커 접근 보장 |
| `objects_` / `proxies_` | 메인 스레드 (open/close) & 워커 스레드 (addMethod) | close()시 unsubscribe로 선제 보호 |

---

## 9. V1 스코프

| 기능 | 포함 | 비고 |
|---|---|---|
| 메시지 기반 메서드 등록 | ✅ | 핵심 기능 |
| Incoming call → actor 라우팅 | ✅ | Async reply (non-blocking) |
| 프록시 호출 | ✅ | handle() 내 동기 호출 (V1) |
| 동적 등록 해제 | ⚠️ 부분 | registrations_ 정리만, sdbus API 확인 후 보강 |
| 여러 object path 지원 | ✅ | addMethod()가 이미 지원 |
| 복잡한 D-Bus 타입 (variant/struct) | ❌ | V1은 string serialization |
| Introspection | ❌ | sdbus-c++가 자동 생성 |
| 프록시 비동기 호출 | ❌ | V2 |

---

## 10. 사용 예시 (외부 액터)

```cpp
// MyActor::open()에서 DBUS 메서드 등록
void MyActor::open() {
    sendMsg("dbus_actor", DbusRegisterMethod{
        .objectPath = "/com/v2/test",
        .interfaceName = "com.v2.test.engine",
        .methodName = "MyMethod",
        .ownerActorName = name()
    });
}

// MyActor::handle()에서 DBUS 호출 수신 및 응답
void MyActor::handle(const Message& msg) {
    std::visit(overloaded{
        [this](const DbusMethodInvocation& inv) {
            std::string result = processMyMethod(inv.args);
            sendMsg("dbus_actor", DbusMethodCallResult{
                inv.callId, result, false
            });
        },
        [this](const DbusRegisterResult& res) {
            if (res.success)
                V2_LOG_INFO("Method registered: %s", res.methodKey.c_str());
            else
                V2_LOG_ERROR("Registration failed: %s", res.errorMsg.c_str());
        },
        // ... other handlers
    }, msg);
}
```

```cpp
// 프록시 호출 예시
void MyActor::callRemoteService() {
    sendMsg("dbus_actor", DbusProxyCallRequest{
        .callId = 1,
        .destination = "com.example.Service",
        .objectPath = "/com/example/service",
        .interfaceName = "com.example.service",
        .methodName = "DoSomething",
        .args = "hello",
        .requesterActorName = name()
    });
}

void MyActor::handle(const Message& msg) {
    std::visit(overloaded{
        [this](const DbusProxyCallResult& res) {
            if (!res.isError)
                V2_LOG_INFO("Proxy result: %s", res.result.c_str());
        },
        // ...
    }, msg);
}
```

---

## 11. close() 시 고려사항

```cpp
int DbusActor::close() {
    if(state_ == Closed) return Ok;
    state_ = Closing;

    // 1. fd 구독 해제 → 더 이상의 sdbus 콜백 방지
    if(auto* d = actorContext() ? actorContext()->dispatcher() : nullptr) {
        if(dbusFd_ >= 0) {
            d->unsubscribe(dbusFd_);
            dbusFd_ = -1;
        }
    }

    // 2. 미처리 pending call 정리
    {
        std::lock_guard<std::mutex> lock(pendingCallsMutex_);
        pendingCalls_.clear();
    }

    // 3. sdbus 객체 정리
    proxies_.clear();
    objects_.clear();
    connection_.reset();
    registrations_.clear();

    state_ = Closed;
    return Ok;
}
```
