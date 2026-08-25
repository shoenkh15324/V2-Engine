# 액터 모델

V² Engine의 모든 구성 요소를 이루는 **액터**의 정의, 생명주기, 통신 방법을 처음 읽는 사람도 따라올 수 있게 정리한 문서.

---

## 목차

- [요약 (세 문장)](#요약-세-문장)
- [개요](#개요)
- [설계 철학](#설계-철학)
- [아키텍처](#아키텍처)
- [핵심 클래스](#핵심-클래스)
  - [Actor](#actor)
  - [ActorSystem](#actorsystem)
  - [ActorHandle](#actorhandle)
  - [ActorRegistry](#actorregistry)
  - [ActorRuntime](#actorruntime)
- [생명주기](#생명주기)
- [통신](#통신)
  - [메시지 전송](#메시지-전송)
  - [타입 안전 디스패치](#타입-안전-디스패치)
  - [타이머](#타이머)
- [결함 내성](#결함-내성)
- [소유권과 계층](#소유권과-계층)

---

## 요약 (세 문장)

1. 시스템의 모든 구성 요소는 **액터**입니다 — 자기 전용 메일박스를 가진 독립된 작업 단위입니다.
2. 액터끼리는 **메시지 전송으로만** 대화하고, 받은 메시지는 한 번에 하나씩 순서대로 처리합니다 — 공유 상태와 락 걱정이 없습니다.
3. 새 액터를 만들려면 `open()`/`close()`/`handle()` 세 메서드만 구현하면 됩니다 — 등록·스케줄링·실패 복구는 시스템이 알아서 합니다.

---

## 개요

회사에 비유하면 이해가 빠릅니다. 각 액터는 **자기 사서함을 가진 직원**입니다. 동료에게 뭘 부탁할 때도 직접 말을 걸지 않고 **이메일(메시지)**을 보내며, 받은 편지는 **도착한 순서대로 한 건씩** 처리합니다. 사무실 벽은 두껍게 칠해져 있어 서로의 책상(내부 상태)을 함부로 만질 수 없습니다 — 덕분에 여러 스레드가 돌아가는 환경에서도 동시성 버그가 구조적으로 생길 수 없습니다.

V² Engine은 긴 시간 실행되는 시스템 데몬 구조화를 위해 평면적이고 격리된 액터 모델을 채택합니다. 시스템 내의 모든 구성 요소 — CLI 명령 처리부터 OS 시그널 처리까지 — 는 각각 독립적인 액터이며, 자체 상태를 소유하고 비동기 메시지 전송을 통해서만 통신합니다.

게임 엔진 액터 모델(예: Unreal Engine의 액터-컴포넌트 계층)과 달리, V² Engine의 액터는 부모-자식 관계나 공간·변환 시스템이 없는 **독립적인 피어**입니다. 시스템 수준 서비스에 최적화된 순수 메시지 전송 프레임워크입니다.

---

## 설계 철학

| 원칙 | 설명 |
|------|------|
| **액터 격리** | 각 액터는 전용 메일박스를 소유하며 메시지를 순차적으로 처리합니다. 액터 간 공유 상태가 없습니다. |
| **핫 패스 락프리** | 메일박스와 디스패치 큐는 락프리 데이터 구조를 사용합니다. 메시지 전달 경로에서 뮤텍스 경쟁이 없습니다. |
| **협력 스케줄링** | 액터는 설정 가능한 배치(기본값: 32)로 메시지 처리 후 양보합니다. 선점은 없습니다. |
| **컴파일 타임 안전성** | `dispatch()` 템플릿은 C++20 `requires` 표현식을 사용하여 메시지 핸들러 커버리지를 정적으로 검증합니다 — 쉽게 말해, 처리한다고 선언한 메시지 타입의 핸들러를 빠뜨리면 프로그램이 아예 빌드되지 않습니다. |
| **세대 기반 참조** | `ActorHandle`은 세대 카운터를 사용하여 `shared_ptr` 오버헤드 없이 만료된 참조를 안전하게 감지합니다. |

---

## 아키텍처

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

**소유 체인:** `ActorSystem`은 `Worker` 스레드와 `ActorRuntime` 인스턴스를 소유합니다. 각 `ActorRuntime`은 하나의 `Actor`와 하나의 `Mailbox`를 래핑합니다. `ActorRegistry`는 조회용 원시 포인터를 보유합니다(세대 검증 포함). `Supervisor`는 설정 가능한 재시작 정책으로 결함을 처리합니다.

---

## 핵심 클래스

### Actor

**파일:** `src/core/actor_system/actor/actor.hpp:20`

모든 액터의 추상 기본 클래스. 생명주기 인터페이스와 메시지 전송 API를 정의합니다.

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

**상태 열거형:**

| 상태 | 값 | 설명 |
|------|---|------|
| `Closed` | 0 | 비활성, 메시지 처리 없음 |
| `Closing` | 1 | 종료 중 전이 |
| `Opening` | 2 | 개시 중 전이 |
| `Opened` | 3 | 활성, 메시지 처리 중 |
| `Inherited` | 4 | 포워딩을 위한 특수 상태 |

---

### ActorSystem

**파일:** `src/core/actor_system/actor_system.hpp:50`

런타임을 소유하는 최상위 시스템. 액터를 생성하고, 워커 스레드를 관리하며, 이벤트 루프를 조율합니다.

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

**생성 흐름:**
1. `nextActorId_++`로 고유 ID 할당
2. `std::make_unique<T>(name, id, args...)`로 액터 생성
3. `Mailbox` + `ActorRuntime` 생성, `ActorRegistry`에 등록

---

### ActorHandle

**파일:** `src/core/actor_system/actor/actor_handle.hpp:8`

액터에 대한 세대 인식 약한 참조. 액터 ID가 재활용될 때 use-after-free를 방지합니다.

```cpp
struct ActorHandle {
    bool    valid() const;          // Generation-check against registry
    Actor*  get() const;            // Resolve to raw pointer
    void    send(Message msg) const; // Send via resolved actor
    uint64_t id() const;
    uint64_t generation() const;
};
```

**동작 원리:** 액터가 레지스트리에서 제거되면 해당 ID 슬롯의 세대 카운터가 증가합니다. 이후의 `handle.get()` 호출은 세대를 확인하고, 만료된 핸들인 경우 `nullptr`을 반환합니다. 이를 통해 `shared_ptr` 없이도 댕글링 참조에 안전합니다.

---

### ActorRegistry

**파일:** `src/core/actor_system/actor/actor_registry.hpp:10`

이중 인덱싱(이름과 ID)을 지원하는 스레드 안전 레지스트리.

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

**파일:** `src/core/actor_system/runtime/actor_runtime/actor_runtime.hpp:19`

`Actor`와 그 `Mailbox`를 래핑합니다. 배치 처리를 구현하고 스케줄러·슈퍼바이저와 통합됩니다.

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

**배치 처리 (`run`):**
1. 워커가 디스패처에서 `ActorRuntime*`을 획득
2. `processBatch(maxBatch)` 호출 — 메일박스에서 메시지 팝
3. 시스템 메시지(`ActorEnableRequest`, `ActorDisableRequest`, `ActorRestartRequest`)를 위해 `tryConsumeLifecycle()`을 먼저 확인
4. 애플리케이션 메시지의 경우 `actor->handle(msg)` 호출
5. 예외 발생 시: `Supervisor::onActorFailed()`에 알림
6. 배치 후 메일박스가 비어있지 않으면: 재디스패치(work converging — 남은 백로그를 같은 워커가 이어 처리)

---

## 생명주기

### 상태 머신

```
Closed → Opening → Opened → Closing → Closed
```

### 기동 시퀀스

1. `ActorSystem::start()` 호출
2. `WorkDispatcher`, `EventLoop`, `Scheduler` 기동
3. 등록된 각 액터에 대해: `actor->open()` 호출 (상태 → `Opened`)
4. `Worker` 스레드 기동

### 런타임 처리

메시지는 배치 단위로 협력적으로 처리됩니다:

```
sema → own queue pop → run(batch=32) → empty 아니면 재디스패치
```

액터는 배치 후 양보하여 액터 간 공정성을 보장합니다.

### 종료 시퀀스

1. `ActorSystem::stop()` 호출
2. `Scheduler`와 `EventLoop` 중지
3. `WorkDispatcher::beginDrain()` — 워커가 미처리 작업을 마치고 종료
4. 모든 `Worker` 스레드 중지
5. 등록된 각 액터에 대해: `actor->close()` 호출 (상태 → `Closed`)

### 소멸

`ActorRuntime::~ActorRuntime()`:
1. 이 액터와 연결된 모든 타이머 취소
2. 레지스트리에서 액터 제거(세대 증가 → 만료된 핸들 무효화)

### 런타임 활성화/비활성화

| 메시지 | 동작 |
|--------|------|
| `ActorEnableRequest` | 상태가 `Closed`이면 `actor->open()` 호출 |
| `ActorDisableRequest` | 상태가 `Opened`이고 액터가 essential이 아니면 `actor->close()` 호출 |

essential 액터(`setEssential(true)`)는 메시지로 비활성화할 수 없습니다.

---

## 통신

### 메시지 전송

모든 전송은 발신자에게 **비동기적이고 논블로킹**입니다.

| 메서드 | 대상 해석 | 타이밍 |
|--------|-----------|--------|
| `sendMsg(name, msg)` | 이름으로 레지스트리 조회 | 즉시 |
| `sendMsg(id, msg)` | ID로 레지스트리 조회 | 즉시 |
| `sendMsgAfter(name, msg, delayMs)` | 이름으로 레지스트리 조회 | 스케줄러 경유 지연 |
| `sendMsgAfter(id, msg, delayMs)` | ID로 레지스트리 조회 | 스케줄러 경유 지연 |

**메시지 흐름:**

```
발신 액터
  → sendMsg("target", msg)
  → 런타임이 ActorHandle 해석 (registry 경유 name → id)
  → 런타임이 대상 메일박스에 큐잉 (락프리 MPSC push)
  → 런타임이 WorkDispatcher에 알림 (dispatch)
  → WorkDispatcher가 대상 워커 큐에 ActorRuntime* 푸시
  → 워커가 획득 (세마포어 웨이크 또는 스틸)
  → ActorRuntime::run(maxBatch)로 메시지 처리
  → Actor::handle(msg) 호출
```

### 타입 안전 디스패치

`dispatch()` 템플릿 메서드는 C++20 `requires` 표현식을 사용하여 컴파일 타임 검증된 메시지 라우팅을 제공합니다:

```cpp
// 이 액터 타입에 대한 메시지 튜플 정의
using CmdActorMessages = std::tuple<CmdRequest, PmuDataUpdate, WifiScanResult, ...>;

void CmdActor::handle(const Message& msg) {
    Actor::dispatch(*this, msg, CmdActorMessages{});  // compile-time checked
}

// 각 구체 타입에 대한 오버로드 핸들러
void CmdActor::handle(const CmdRequest& m) { ... }
void CmdActor::handle(const PmuDataUpdate& m) { ... }
```

메시지 타입이 튜플에 나타나지만 해당 `handle(T)` 오버로드가 없으면 코드가 컴파일되지 않습니다 — 런타임이 아닌 빌드 타임에 누락된 핸들러를 캐치합니다.

### 타이머

```cpp
int  startTimer(Message msg, uint64_t delayMs, bool repeating);
void cancelTimer(int timerId);
void cancelAllTimers();
```

- 타이머 콜백은 메시지를 복제하여 대상 액터의 메일박스에 큐잉합니다
- `ActorRuntime::~ActorRuntime()`은 모든 타이머를 자동 취소합니다

---

## 결함 내성

### 슈퍼바이저

**파일:** `src/core/actor_system/runtime/supervisor/supervisor.hpp:30`

| 전략 | 동작 |
|------|------|
| `OneForOne` | 결함이 발생한 액터만 재시작 (`maxRestarts`까지) |
| `OneForAll` | 모든 액터에 `ActorRestartRequest` 브로드캐스트 |
| `None` | 결함이 발생한 액터를 영구 종료 |

### 결함 처리 흐름

메시지 처리 중 `onActorFailed()`가 호출되면:

1. **데드 레터**: 결함 메시지 + 남은 모든 메일박스 메시지가 `DeadLetterQueue`로 이동
2. **전략 적용**:
   - `OneForOne`: 재시작 카운터의 원자 CAS로 `runtime->tryRestart(reason, limit)`
   - `OneForAll`: 모든 액터에 `ActorRestartRequest` 브로드캐스트
   - `None`: `runtime->shutdown()`으로 영구 종료
3. **예산 검사**: 최대 재시작 횟수 초과 시 액터가 영구 종료됩니다

### 액터별 정책 오버라이드

```cpp
supervisor->setStrategy(actorId, RestartStrategy::None);  // disable restart for specific actor
supervisor->setDefaultStrategy(RestartStrategy::OneForAll); // system-wide default
```

---

## 소유권과 계층

V² Engine의 액터는 **평면 피어**입니다 — 부모-자식 계층이 없습니다.

```
ActorSystem
  ├── Actor A (standalone)
  ├── Actor B (standalone)
  ├── Actor C (standalone)
  └── ...
```

각 액터는:
- 단일 글로벌 `ActorRegistry`에 등록됨
- 다른 모든 액터로부터 독립적
- 비동기 메시지를 통해서만 통신

이 평면 설계는 액터 상태에 대한 추론을 단순화하고 계층적 생명주기 관리의 복잡성을 제거합니다. 액터 간 의존성은 소유권이 아닌 메시지 프로토콜로 표현됩니다.
