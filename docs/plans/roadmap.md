# V2-Engine 로드맵

---

## 일정

```
Phase 1: 성능 병목 제거 ✅ 완료
Phase 2: actor_system 리팩토링 ✅ 완료
Phase 3: 메모리/전송 최적화 ✅ 완료
Phase 4: 아키텍처 고도화 🔄 진행 중
Phase 5: 벤치마크 인프라 + 보고서 + CI/테스트 확대 ⬜ 대기 
```

---

## Phase 1: 성능 병목 제거 ✅

> **목표**: `MutexMailbox` → `LockFreeMpscQueue` + 전역 뮤텍스/세마포어 제거

### Lock-free 메일박스

| 작업 | 상세 |
|------|------|
| `LockFreeMpscQueue<T>` | Vyukov MPSC, placement new, `hardware_destructive_interference_size` |
| `ActorSystem` 통합 | `createActor()`에서 `MutexMailbox` 대신 `LockFreeMpscQueue<Message>` 직접 생성 |
| 테스트/벤치마크 | `test_mailbox.cpp` → `test_mpsc_queue.cpp`, `mailbox_bench.cpp` → `mpsc_queue_bench.cpp` |

### Per-Worker 디스패처

| 작업 | 상세 |
|------|------|
| Per-Worker MPSC 큐 | 전역 `readyQueue_` + `mutex_` → 워커별 `LockFreeMpscQueue<ActorRuntime*>` |
| Per-Worker 세마포어 | 전역 `counting_semaphore` → 워커별 세마포어 (thundering herd 제거) |
| 액터-워커 악피니티 | `hash(actorId) % workerCount`으로 고정 배정 → 한 액터를 한 워커만 처리 |
| `inQueue_` 제거 | `ActorRuntime::scheduled_` 원자적 교환으로 dedup 대체 |

---

## Phase 2: actor_system 리팩토링

> **목표**: 강결합 구조 해소 → Runtime과 Actor 완전 분리, 단방향 의존성, 컴파일 의존성 최소화, 확장 가능한 구조 확보


### 디렉토리 구조 (현재)

```
actor_system/
    ├── actor_system.hpp/cpp       # Facade
    ├── actor_system_impl.hpp
    │
    ├── actor/
    │   ├── actor.hpp/cpp          # Actor 추상 클래스
    │   └── actor_handle.hpp/cpp   # generation 기반 safe reference
    │
    ├── runtime/
    │   ├── i_actor_runtime.hpp    # Actor가 의존할 인터페이스
    │   ├── actor_runtime.hpp/cpp  # IActorRuntime 구현체
    │   ├── i_scheduler.hpp        # Scheduler 인터페이스
    │   ├── scheduler.hpp/cpp
    │   ├── i_actor_registry.hpp   # Registry 인터페이스
    │   ├── actor_registry.hpp/cpp
    │   │
    │   └── dispatcher/
    │       ├── i_work_dispatcher.hpp  # WorkDispatcher 인터페이스
    │       ├── work_dispatcher.hpp/cpp
    │       ├── worker.hpp/cpp
    │       └── io/
    │           ├── i_event_loop.hpp       # EventLoop 인터페이스
    │           ├── event_loop_epoll.hpp   # epoll 구현체
    │           └── event_loop_epoll.cpp
    │
    ├── messages/                   # Message 정의 (서브시스템별 분할)
    │   ├── message.hpp
    │   ├── message_traits.hpp
    │   ├── cmd_messages.hpp
    │   ├── ipc_messages.hpp
    │   ├── dbus_messages.hpp
    │   ├── device_manager_messages.hpp
    │   ├── monitor_messages.hpp
    │   ├── system_messages.hpp
    │   ├── tick_messages.hpp
    │   └── network_manager/
    │       ├── network_manager_messages.hpp
    │       └── wifi_messages.hpp
    │
    └── detail/                    # (예비)
```

### 핵심 아키텍처 변경

#### 1. Actor ↔ ActorRuntime 순환 의존 제거 ✅

**기존 문제**: Actor가 ActorRuntime을 직접 참조하고, ActorRuntime이 Actor를 참조하는 양방향 의존

**해결**: `IActorRuntime` 인터페이스 도입으로 단방향 의존성 확보

```cpp
// actor/i_actor_runtime.hpp
class IActorRuntime {
public:
    virtual void send(ActorId target, MessageEnvelope msg) = 0;
    virtual void reply(ActorId from, MessageEnvelope msg) = 0;
    virtual void schedule(MessageEnvelope msg, uint64_t delayMs) = 0;
    virtual ActorId self() const = 0;
    virtual void stop() = 0;
    virtual ActorId spawn(std::unique_ptr<Actor> actor) = 0;
};
```

#### 2. Dispatcher 역할 분리 ✅

**기존 문제**: Dispatcher가 epoll + Worker 관리 + Actor Dispatch + Scheduling 모두 담당

**해결**: 역할별 분리 + 인터페이스 기반 의존성 역전

| 컴포넌트 | 책임 | 파일 |
|----------|------|------|
| `IWorkDispatcher` | Ready Actor Queue 인터페이스 | `dispatcher/i_work_dispatcher.hpp` |
| `WorkDispatcher` | MPSC 큐 + 세마포어 기반 work 분배 | `dispatcher/work_dispatcher.hpp/cpp` |
| `IEventLoop` | fd 구독/구독해제 인터페이스 | `dispatcher/io/i_event_loop.hpp` |
| `EventLoopEpoll` | epoll 기반 이벤트 루프 (Linux 전용) | `dispatcher/io/event_loop_epoll.hpp/cpp` |
| `Scheduler` | Timer Queue, Timeout 관리 (`IEventLoop`에 의존) | `runtime/scheduler.hpp/cpp` |

##### 의존성 흐름

```
ActorSystem
  ├── WorkDispatcher (큐/세마포어만)
  ├── EventLoopEpoll (epoll만, Linux 전용)
  ├── Scheduler → IEventLoop에 의존
  ├── Workers[] → IWorkDispatcher에만 의존
  └── ActorRuntime → IWorkDispatcher에만 의존
```

#### 3. Registry 역할 축소 ✅

**기존 문제**: Registry가 등록/조회/삭제/생명주기 모두 담당

**해결**: Lookup 전용으로 축소, enableActor/disableActor는 IActorRuntime으로 이동

```cpp
// IActorRegistry — 순수 lookup (반환 타입별 네이밍)
class IActorRegistry {
    virtual ActorHandle findHandleByName(const std::string& name) = 0;
    virtual ActorHandle findHandleById(uint64_t id) = 0;
    virtual Actor* findActorByName(const std::string& name) = 0;
    virtual Actor* findActorById(uint64_t id) = 0;
    virtual Actor* resolve(const ActorHandle& handle) const = 0;
    virtual void add(Actor* actor) = 0;
    virtual void remove(Actor* actor) = 0;
    virtual void clear() = 0;
};

// ActorHandle — generation 기반 safe reference
class ActorHandle {
    uint64_t id_;
    uint64_t generation_;
    IActorRegistry* registry_;
    bool valid() const;
    Actor* get() const;
    void send(Message msg);
};
```


### 의존성 방향 (최종)

```
Application
      │
      ▼
ActorSystem
      │
      ├── WorkDispatcher ──► IWorkDispatcher (interface)
      │       └── Worker
      │
      ├── EventLoopEpoll ──► IEventLoop (interface)   [Linux 전용]
      │
      ├── Scheduler ──► IEventLoop (interface)
      │       └── Timer
      │
      ├── ActorRegistry ──► IActorRegistry (interface)
      │       └── ActorHandle (generation-based safe reference)
      │
      └── ActorRuntime ──► IActorRuntime (interface)
              ├── IWorkDispatcher*
              ├── IScheduler*
              ├── IActorRegistry*
              └── IEventLoop*

Actor ──► IActorRuntime* (forward decl, friend: ActorRuntime)
ActorHandle ──► IActorRegistry* (forward decl, resolve via generation)
```

**모든 의존성은 인터페이스를 통해 흐르고, 구체 클래스는 ActorSystem에서만 직접 참조합니다.**
**ActorRegistry는 런타임 시 lock-free, write ops는 start() 전에만 발생합니다.**

---

## Phase 3: 메모리/전송 최적화 ✅

> **목표**: 핫 패스 캐시 미스 + 불필요한 할당/잠금/원자 연산 제거

### 메시지 전송 경로 최적화

> **문제**: `sendMsg()` 호출당 `ActorRegistry` 뮤텍스 + `unordered_map` 조회 → 매 메시지마다 futex + 해시 체인 순회

| 작업 | 상세 |
|------|------|
| 생성자 문자열 이동 ✅ | `Actor::Actor(name)` → `name_(std::move(name))` 불필요한 복사 제거 (`c1e091b`) |

### 캐시 라인 패딩 ✅

> **문제**: 빈번한 원자적 접근 필드가 인접 필드와 같은 캐시 라인에 위치 → false sharing

| 작업 | 상세 |
|------|------|
| `ActorRuntime::scheduled_` ✅ | 프로듀서 쓰기 / 워커 읽기 간 캐시 라인 분리 (`alignas(64)`) |
| `ActorMetrics` ✅ | 6개 atomic을 48바이트에 패킹 → `alignas(64)`로 분리 (`metrics.hpp:8-14`) |
| `WorkerMetrics` / `DispatcherMetrics` ✅ | 인접 atomic 패딩 추가 (`metrics.hpp:46-80`) |
| `kCacheLine` 상수 공유 ✅ | `lock_free_mpsc_queue.hpp` → `common/`으로 이동, 전역 사용 |

### 타이머 메모리 할당 제거 ✅

> **문제**: `Scheduler::addTimer()` 호출당 3~4회 힙 할당 (`shared_ptr<Message>`, 람다 캡처, `TimerNode`, `std::function`)

| 작업 | 상세 |
|------|------|
| `shared_ptr<TimerNode>` 제거 ✅ | `TimerNode`를 풀 할당 또는 스택 배치로 변경 |
| `shared_ptr<Message>` 제거 ✅ | `Scheduler::addTimer()`에서 Message를 `TimerNode` 내에 직접 저장 |
| `std::function` 제거 ✅ | `Timer::Callback`을 직접 호출 가능한 타입으로 변경 (함수 포인터 + void* 또는 CRTP) |

### Slab 기반 메모리 풀 ✅

> **목표**: TCMalloc/jemalloc 컨셉 기반 계층형 할당기로 작은 객체의 힙 할당 비용 제거

| 작업 | 상세 |
|------|------|
| `FreeList` | Intrusive singly-linked list, O(1) push/pop, 블록 내부에 `next` 포인터 저장 |
| `Chunk` | 4KB 정렬 메모리 블록을 blockSize로 분할, 내부 FreeList 관리 |
| `Slab` | SizeClass별 Chunk 컬렉션 관리, mutex 기반 스레드 안전, partial chunk 추적 |
| `SizeClass` | 9개 size class 테이블 (8B~2048B), batchSize별 배치 할당 |
| `ThreadLocalCache` | per-thread FreeList, batch fetch/return로 중앙 락 비용 최소화 |
| `MemoryPoolT` | Policy-based singleton, `allocate<T>()`/`deallocate<T>()` 템플릿 API |
| Over-aligned 지원 | `alignof(T) > max_align_t` 시 `::operator new` fallback |
| Large 할당 | `sizeof(T) > 2048` 시 일반 heap fallback |
| DEBUG 정책 | `PoisonDebugPolicy` — deallocate 시 `0xCD` 패턴 덮어쓰기 (use-after-free 탐지) |
| Noexcept 정책 | `NoexceptAllocPolicy` — 할당 실패 시 `std::abort()` (임베디드용) |

**구조**:
```
MemoryPool (Singleton)
  └── ThreadLocalCache (per-thread, lock-free fast path)
        └── Slab (per-SizeClass, mutex)
              └── Chunk (4KB, blockSize 단위 분할)
                    └── FreeList (intrusive linked list)
```

### 메시지 시스템 타입 에러제이션 ✅

> **문제**: `std::variant<Msg1, ..., Msg33>` — 모든 메시지 헤더가 컴파일되고, `sizeof(Message)` = 208바이트
>
> **해결**: SBO (Small Buffer Optimization, 64바이트) + MemoryPool 기반 커스텀 타입 에러제이션 클래스 `Message` 도입.
> `sizeof(Message)` = 72바이트로 감소. 작은 메시지는 stack/queue slot에 inline 저장, 큰 메시지는 MemoryPool에 할당.
> `MessageId` enum + `static constexpr kId`로 타입 식별, `switch + as<T>()`로 디스패치.

| 작업 | 상세 |
|------|------|
| `Message` 클래스 도입 ✅ | SBO (≤64B) + MemoryPool fallback, placement new 저장 |
| `MessageId` enum ✅ | 40개 메시지 타입 ID 정의 (`message_traits.hpp`) |
| `switch + as<T>()` 패턴 ✅ | 각 Actor `handle()`에서 switch-on-id로 타입 복원 |
| `LockFreeMpscQueue<Message>` ✅ | variant 제거, 72B 고정 슬롯으로 queue slot 크기 65% 감소 |

### 전역 로깅 뮤텍스 제거 ✅

> **문제**: `logPrint()`가 모든 워커 스레드를 하나의 `gMutex`로 직렬화

| 작업 | 상세 |
|------|------|
| `thread_local` 버퍼 ✅ | per-thread 버퍼에 누적, 임계치 도달 시만 flush |
| `std::format` 전환 ✅ | printf `%s`/`%d` → `{}`, 컴파일 타임 포맷 검증, `.c_str()` 제거 |
| `gLevel` → `std::atomic` ✅ | 멀티스레드 안전성 확보 |
| TLS 소멸자 기반 flush ✅ | `LogBuffer` RAII — 프로그램/스레드 종료 시 자신의 버퍼를 안전하게 drain (`atexit` + `thread_local`의 소멸 순서 UAF 해결) |
| stderr lock-free + 파일 mutex ✅ | POSIX stderr는 별도 락 불필요, 파일만 `std::mutex`로 보호 (flush 시에만) |

### 메모리 순서 최적화 ✅

> **문제**: 핫 패스 원자들이 기본 `seq_cst` 사용 → ARM에서 불필요한 `DMB ISH` 배리어

| 작업 | 상세 |
|------|------|
| `running_` 플래그 ✅ | Dispatcher, Worker, EventLoopEpoll, MainApp, TuiApp load → relaxed, store → release |

### 메트릭 핫 패스 최적화 ✅

> **문제**: `count()`가 metrics 활성화 여부와 무관하게 매 메시지마다 두 번의 atomic 로드 수행

| 작업 | 상세 |
|------|------|
| 지연 `count()` ✅ | `Metrics::recordDispatch/recordEnqueue` 내부에서만 `count()` 호출하도록 변경 |
| 메트릭 비활성화 시 zero-overhead ✅ | `isEnabled()` 체크를 호출 전으로 이동, 비활성화 시 atomic 로드 0회 |

### ActorRegistry 락 분할 ✅

> 단일 `std::mutex`로 모든 연산 직렬화 → 읽기/쓰기 분리 (C++17 `std::shared_mutex`, 외부 의존 없음)

- ✅ `std::shared_mutex` 전환 + read/write 락 배분 (`findHandle*`/`findActor*`/`forEachActor` → shared, `add`/`remove` → unique)
- ✅ `findActorByName`/`findActorById` 추가 + `Actor::sendMsg`/`sendMsgAfter`의 `valid()` 중복 검증 제거 (조회 3회 → 1회)
- ✅ `forEachActor`는 락 해제 후 콜백 실행 (스냅샷 후 콜백 — 콜백 내 registry 수정 방지)

---

## Phase 4: 아키텍처 고도화 🔄

> **목표**: 정확성 + 타입 안전 + 장애 처리 + 로드 밸런싱
>
> **진행 순서**: Supervision → 정확성 버그 → typed_channel → 로드 밸런싱/병렬화 → 스케일링 → 하드닝 → 서비스 정리

### 4-1. Supervision 트리 + 예외 격리 ✅

> **문제**: `handle()` 예외 시 워커 스레드 크래시 → 프로세스 전체 종료, 복구 불가

| 작업 | 상세 |
|------|------|
| `supervisor.hpp` ✅ | 액터 실패 처리/재시작 구조 (`runtime/supervisor/`) |
| 예외 격리 ✅ | `ActorRuntime::run()`에서 `try/catch`로 `handle()` 감싸기, 크래시된 액터 격리 |
| 재시작 전략 ✅ | `OneForOne` (실패 액터만), `OneForAll` (전체 재시작, `ActorRestartRequest` 브로드캐스트) |
| 재시작 예산 ✅ | `maxRestarts` 한도 초과 시 액터 shutdown (`OneForOne`/`OneForAll` 모두), 콜백 예외 격리 |
| 데드 레터 큐 ✅ | 실패한 메시지를 보관하는 큐 (`dead_letter_queue.hpp`) |

### 4-2. 정확성 버그 수정 ✅

> **문제**: 동시성 레이스 + 수명 주기 안전성 결여

| 작업 | 상세 |
|------|------|
| 로그 버퍼 exit UAF ✅ | `atexit(logFlush)` + `thread_local gBuf`의 소멸 순서 역전으로 종료 시 use-after-free → `LogBuffer` TLS RAII로 전환 (`log.cpp`, `da133f1`) |
| epoll 중복 구독/핸들러 레이스 ✅ | `EventLoopEpoll::subscribe()`가 비-loop 스레드에서 중복 감지 실패 → `handlers_` mutex 보호 + 동기 중복 확인 (`da133f1`) |
| 타이머 use-after-free ✅ | `Scheduler::addTimer()`에서 `Actor*` raw 포인터 캡처 → 수명 안전 처리 (`cde6493`) |
| `ActorState` 레이스 ✅ | `state_`를 `std::atomic<ActorState>`로 전환 — 전이는 owner 스레드에서 store, 관측자는 load (스레드 프리 원칙은 액터의 처리 상태에 적용, 수명주기 관측 메타데이터는 원자적 read로 안전화) |
| `ActorStateChanged` 메시지 제거 ✅ | 발신자(`setState()`)가 미사용, 수신측(Monitor)은 로그만 출력 → 메시지·열거값·핸들러 제거. 상태 관측은 `getState()` 폴링으로 통일 |
| 그레이셔널 드레인 ✅ | `ActorSystem::stop()` 시 미처리 메시지 처리 완료 후 중지 → 드레인 단계 추가 |
| `Worker::stop()` 데드락 ✅ | 세마포어 해제 없이 `join()` 호출 시 데드락 가능성 점검 |

### 4-3. 타입별 메시지 디스패치 ✅

> **문제**: `switch(msg.id())` + `msg.as<T>()`(무검증 `static_cast`)는 id와 저장 타입이 어긋나면 UB. 수신 계약이 없어 못 받는 타입이 액터별 `default` 분기에서 조용히 유실
>
> **방향**: type-erased 저장(64B SBO/MemoryPool)은 유지하고 `std::visit` 시맨틱만 재구현. **전역 카탈로그 대신 액터별 수신 튜플** 채택 — 수신 계약이 `dispatch` 호출 지점에 명시되고 `static_assert`가 튜플 ⊆ 핸들러를 컴파일 타임에 강제 (전역 카탈로그는 nlohmann/json 등 전체 메시지 헤더를 모든 액터 TU로 전파해 컴파일 타임 증가)

| 작업 | 상세 |
|------|------|
| `Message::visit<Tuple>` ✅ | tuple fold short-circuit으로 id 일치 시에만 `as<T>` → UB 구조적 제거 |
| `Actor::dispatch` + `handleUnknown` ✅ | 수신 튜플 주입 + static_assert(튜플 ⊆ 핸들러, 핸들러 public 필수) + 데드 레터 기본 경로(로그 + `dead_letter` 카운터) |
| 액터 순차 전환 ✅ | 9개 서비스 액터 + integration/standalone 테스트 액터 3개: `handle(const Message&)` 1줄 + `handle(const SpecificMsg&)` 오버로드. CmdActor는 `Actor::dispatch` 정규화 호출(이름 숨김), 기존 `default: break;`(조용한 유실) → 데드 레터 메트릭으로 개선 |
| 유닛 테스트 ✅ | `test_message_typed_dispatch.cpp` — 타입 라우팅 / 튜플 밖 id → 데드 레터 / 핸들러 없는 타입 static_assert 거부 |

### 4-4. 로드 밸런싱 & 병렬화 (액터 모델 보존) ✅

> **문제**: 고정 악피니티(`actorId % N`)로 워커 간 불균형 + 단일 핫 액터의 메일박스 push 경합
>
> **원칙**: 액터 모델 3대 보증(① 상태 캡슐화 ② 단일 스레드 실행 ③ 메시지 통신)을 **깨지 않고** 처리량/균형 확보
>
> **메모**: 기법은 3갈래로 나뉜다 — **A. 실행 분배**(병렬화) / **B. 처리 효율** / **C. 생산자·경합 최적화**. "단일 핫 액터 병렬화"는 가변 상태 공유 때문에 액터 모델에서 불가능하므로, 핫 액터의 실질적 개선은 **C**(push 경합 제거)가 담당한다.

#### 0. 선행 조건 (필수) — 단일 엔트리 스케줄링 가드

| 작업 | 상세 | 상태 |
|------|------|------|
| `ActorRuntime::scheduled_` 원자 교환 dedup ✅ | enqueue 시 `exchange(true)`가 0→1일 때만 `dispatch()`. run 마지막 배치에서 빈 메일박스 재확인 후 스케줄 상태 정합. `alignas(kCacheLine)`로 캐시 라인 분리, 실패 시 `dispatch()` 롤백으로 실행 토큰 손실 방지. finalize 재스케줄은 `redispatch()`(pendingWork_ 무증가)로 정산 유지 | ✅ **구현 완료** (`actor_runtime.cpp` enqueue/run, `scheduled_`/`inQueue_` 없음 → `scheduled_` 원자 교환). `deduplicated` 메트릭 연동 — 유닛 테스트로 가드 효과 검증(메시지 10000건에 실제 dispatch 1회) |

> **왜 필수인가**: 기존 `enqueue()`는 메시지 1건마다 무조건 `dispatch()` → 액터의 실행 토큰이 **여러 개** 생길 수 있었음. 고정 악피니티가 "한 워커가 순차 소비"로 이걸 감춰줬지만, A 갈래 기법(토큰이 워커를 떠돎)을 적용하면 **같은 액터가 동시에 두 워커에서 실행** → 메일박스(MPSC) 단일 소비자 규약 위반 + 액터 상태 레이스. (이제 `scheduled_` 가드로 해소되어 로드 어웨어/스틸링 진행 가능)

#### 1. A. 실행 분배 — "어느 워커가 실행할까" (액터 간 병렬화)

| 우선순위 | 기법 | 쉽게 설명 | 방식 | 기대 효과 | 특징/비용 |
|---|---|---|---|---|---|
| ★★★ | **로드 어웨어 디스패치** ✅ | 실행 토큰을 놓을 때부터 "가장 덜 바쁜 워커 앞에" 놓는다. 요리사 비유: 일이 쌓이기 전에 덜 바쁜 요리사에게 배정 | 사전(예방형) | 워커 균형 + **백프레셔(큐 1024 가득) 예방** — 70% 임계 초과 시 다른 워커로 라우팅 | 구현 완료 (`work_dispatcher.cpp` `pickWorker`/`pickLeastLoaded`, `highWatermark = kQueueCapacity*7/10`). 라우팅 시 큐 깊이 조회 비용. 액터 지역성 감소(홈 워커 이탈). **로드 = 큐 깊이만 반영, in-flight 실행 토큰 미반영** → 아래 "워커 in-flight 카운터" 작업으로 보강 |
| ★★☆ | **워크 스틸링** ✅ | 유휴 워커가 바쁜 워커의 토큰을 가져간다. 요리사 비유: 내 앞엔 없는데 저 요리사 앞엔 쌓였다 → 가져와서 함 | 사후(반응형) | 유휴 워커 활용, 이미 생긴 불균형 치유 | 구현 완료 (`0a49b63`). per-worker 큐를 `LockFreeMpmcQueue`로 전환 + 유휴 시 이웃 큐에서 steal. 세마포어는 순수 wake 신호로만 사용(`try_acquire_for`로 토큰 좌초 방지), `idleBackoff_`로 busy/idle 스틸 간격 적응. 배수 `busyStealIntervalUs=200` / `idleStealIntervalUs=2000` (config에서 조절 가능) |

> **선택**: 스틸링(사후)과 로드 어웨어(사전)는 **합성 가능** — 사전 예방 + 사후 치유. 단순성과 백프레셔 방지 목적이라면 **로드 어웨어를 먼저** 진행. (아래 "진행 순서 제안" 참고)

#### 2. B. 처리 효율 — "워커 시간을 어떻게 아끼나"

| 상태 | 기법 | 쉽게 설명 | 기대 효과 |
|---|---|---|---|
| ✅ 이미 | **배치 처리** (`maxBatch=32`) | 실행권 1개로 메시지 N개 처리 | 스케줄링 오버헤드 절감 |
| ✅ 이미 | **I/O 오프로드** (`IEventLoop`) | 블로킹 I/O를 이벤트 루프에 위임 | 워커 스레드 비블로킹 |

#### 4. 검증 지표 (로드 밸런싱 메트릭)

| 지표 | 용도 |
|---|---|
| `stealCount` / `stealFailCount` ✅ | 스틸 시도·성공·실패 관찰 — `DispatcherMetrics`에 추가, `v2_cli` metrics 출력(`Steals`/`StealFails`) |
| 워커별 큐 깊이 / `busyTimeNs` / `idleTimeNs` | 워커 균형 관찰 |
| `deduplicated` ✅ | 단일 엔트리 가드 효과 — 가드 구현 후 실제 증가. 유닛 테스트로 검증(`dispatchCount - deduplicated` = 실제 dispatch 1회, 워커 가동 시 redispatch 기록 증가분 주의) |

#### 5. 진행 순서 제안

1. **단일 엔트리 가드** ✅ — `scheduled_` 구현 + `deduplicated` 메트릭 연동 (완료, 유닛 테스트 포함)
2. **로드 어웨어 디스패치** ✅ — 예방형, 백프레셔 방지 (완료, `highWatermark` 임계 + `pickLeastLoaded` 라우팅)
3. **워크 스틸링** ✅ — 치유형, 로드 어웨어와 합성 (완료, `LockFreeMpmcQueue` + `try_acquire_for` 유휴 감지 + 적응형 백오프)

---

## 4-5. 스케일링 성능 붕괴 해소 — 구조적 리팩토링 🔄

> **목표**: 멀티 워커/액터 확장 시 처리량 붕괴의 근본 원인 제거
>
> **방법**: `scheduled_` flag 기반 per-actor dedup을 `WorkDispatcher` 내부의 `inFlight_` 플래그로 교체하여 seq_cst fence를 제거

### 문제 상황

**throughput 벤치 (w=4, 단일 프로듀서):**

| actors | throughput | w=1 대비 |
|--------|-----------|---------|
| 1 | 10.3M | 1.0x |
| 2 | 2.9M | 3.6x ↓ |
| 3 | 1.3M | 7.9x ↓ |
| **4** | **220K** | **46.9x ↓** |

> **핵심**: `actors = workers`일 때 최악. 단순 fence 최적화로는 해결 불가.

### 근본 원인 (3가지)

#### 1. `scheduled_` flag + Dekker handshake

현재 `enqueue()`와 `run()`은 seq_cst fence로 상호 배타성을 보장합니다:

```
enqueue(): seq_cst exchange (LOCK XCHG ~30ns)
run():     seq_cst store (MFENCE ~30ns) + fence (MFENCE ~30ns) + seq_cst exchange (LOCK XCHG ~30ns)
```

**문제점**:
- x86에서 `seq_cst exchange`는 이미 full barrier → `seq_cst fence`가 중복
- N개 액터가 각각 독립적으로 handshake → fence 비용 N배
- `MFENCE`는 x86에서 ~30ns, 4회/메시지 사이클 → ~120ns/메시지

#### 2. `acquire()` 루프의 semaphore 대기

워커가 자기 큐가 비면 `busyStealIntervalUs_=200μs` 동안 semaphore 대기:

```cpp
// 현재 work_dispatcher.cpp:66
semas_[workerId]->try_acquire_for(std::chrono::microseconds(interval));
```

**문제점**:
- actors = workers일 때, 모든 워커가 동시에 유휴
- 200μs 대기 × W개 워커 = 수 ms 파이프라인 블로킹

#### 3. `count()` 기반 `pickWorker` 캐시라인 경합

```cpp
// 현재 work_dispatcher.cpp:115-118
int WorkDispatcher::pickWorker(uint64_t actorId){
    int home = static_cast<int>(actorId % workerCount_);
    if(workerCount_ <= 1) return home;
    if(queues_[home]->count() < static_cast<size_t>(highWatermark_)) return home;
    return pickLeastLoaded(actorId);
}
```

**문제점**:
- `count()`는 `head_`/`tail_` 두 atomic 로드
- head_는 워커(소비자), tail_은 생산자에 의해 수정 → 캐시라인 바운스
- N 액터 × 배치당 redispatch = N × count() 호출

### 해결책: `inFlight_` 플래그 기반 dedup

**핵심 아이디어**: per-actor `scheduled_` flag를 `WorkDispatcher` 내부의 `inFlight_` 벡터로 교체

| 비교 | 현재 (scheduled_) | 변경 후 (inFlight_) |
|------|------------------|---------------------|
| 소유 위치 | `ActorRuntime` (per-actor) | `WorkDispatcher` (중앙 관리) |
| 메모리 순서 | `seq_cst` (4회/메시지) | `acq_rel` (1회/배치) |
| fence | MFENCE 4회 | 0회 |
| dedup 로직 | `exchange(true)` → `dispatch()` 호출 | `exchange(1)` → 큐 push |

### 변경 대상 파일

| 파일 | 변경 내용 |
|------|----------|
| `actor_runtime.hpp` | `scheduled_` 필드 제거 |
| `actor_runtime.cpp` | `enqueue()`: fence+exchange 제거, `run()`: Dekker handshake 제거, `clearInFlight()` 호출 |
| `work_dispatcher.hpp` | `InFlightFlag` 구조체 + `inFlight_` 벡터 추가, `clearInFlight()` 메서드 추가 |
| `work_dispatcher.cpp` | `dispatch()`: inFlight dedup 추가 |
| `worker.cpp` | `onWorkDone()` 호출 제거 (worker는 run만 호출) |
| `i_work_dispatcher.hpp` | `onWorkDone()` → `clearInFlight(uint64_t)` 시그니처 변경 |

### 상세 변경 사항

#### 1. `ActorRuntime` — `scheduled_` 제거

```cpp
// BEFORE (actor_runtime.hpp)
alignas(kCacheLine) std::atomic<bool> scheduled_{false};

// AFTER — 필드 자체 제거
```

#### 2. `enqueue()` — 단순화

```cpp
// BEFORE (actor_runtime.cpp:35-50)
void ActorRuntime::enqueue(Message msg){
    if(!mailbox_->push(std::move(msg))){ return; }
    V2_METRICS()->recordEnqueue(actor_->id(), true, mailbox_->count());
    if(!scheduled_.exchange(true, std::memory_order_seq_cst)){  // ← 제거: seq_cst fence
        if(workDispatcher_ && !workDispatcher_->dispatch(this)){
            scheduled_.store(false, std::memory_order_seq_cst); // ← 제거
        }
    }else{
        V2_METRICS()->recordDispatch(true, 0);
    }
}

// AFTER
void ActorRuntime::enqueue(Message msg){
    if(!mailbox_->push(std::move(msg))){ return; }
    V2_METRICS()->recordEnqueue(actor_->id(), true, mailbox_->count());
    if(workDispatcher_) workDispatcher_->dispatch(this);  // 매번 호출, dedup은 dispatch 내부
}
```

#### 3. `dispatch()` — inFlight dedup 추가

```cpp
// BEFORE (work_dispatcher.cpp:44-48)
bool WorkDispatcher::dispatch(ActorRuntime* actorRuntime){
    bool ok = enqueueEntry(actorRuntime);
    if(ok) pendingWork_.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

// work_dispatcher.hpp에 추가
static constexpr size_t kMaxActors = 1024;
struct alignas(kCacheLine) InFlightFlag {
    std::atomic<uint8_t> v{0};
};
std::vector<InFlightFlag> inFlight_;  // kMaxActors 크기

// AFTER
bool WorkDispatcher::dispatch(ActorRuntime* actorRuntime){
    uint64_t actorId = actorRuntime->actor()->id();
    
    // inFlight dedup — 이미 실행 중이면 중복 방지
    if(inFlight_[actorId % kMaxActors].v.exchange(1, std::memory_order_acq_rel)){
        return true;  // 이미 큐에 있음
    }
    
    int workerId = pickWorker(actorId);
    if(!queues_[workerId]->push(actorRuntime)){
        std::lock_guard lock(mutex_);
        pendingActorList_.push_back(actorRuntime);
        return true;
    }
    pendingWork_.fetch_add(1, std::memory_order_relaxed);
    semas_[workerId]->release();
    return true;
}
```

#### 4. `run()` — Dekker handshake 제거

```cpp
// BEFORE (actor_runtime.cpp:52-77)
int ActorRuntime::run(int maxBatch, bool* hasMoreWork){
    if(hasMoreWork) *hasMoreWork = false;
    auto startTime = Time::now();
    auto r = processBatch(maxBatch);
    uint64_t gapNs = Time::toNs(Time::now() - startTime);
    V2_METRICS()->recordHandle(actor_->id(), r.processed, gapNs);
    
    bool resumed = false;
    if(r.hasMoreWork && workDispatcher_){
        resumed = workDispatcher_->redispatch(this);
    }
    if(!resumed){
        scheduled_.store(false, std::memory_order_seq_cst);     // ← 제거
        if(!stopped_.load(std::memory_order_relaxed) && !mailbox_->empty()){  // ← 제거
            if(!scheduled_.exchange(true, std::memory_order_seq_cst)){        // ← 제거
                if(workDispatcher_ && !workDispatcher_->redispatch(this)){
                    scheduled_.store(false, std::memory_order_seq_cst);       // ← 제거
                }else{
                    resumed = true;
                }
            }
        }
    }
    if(hasMoreWork) *hasMoreWork = resumed;
    return r.processed;
}

// AFTER
int ActorRuntime::run(int maxBatch, bool* hasMoreWork){
    if(hasMoreWork) *hasMoreWork = false;
    auto startTime = Time::now();
    auto r = processBatch(maxBatch);
    uint64_t gapNs = Time::toNs(Time::now() - startTime);
    V2_METRICS()->recordHandle(actor_->id(), r.processed, gapNs);
    
    bool resumed = false;
    if(r.hasMoreWork && workDispatcher_ && !stopped_.load(std::memory_order_relaxed)){
        // 더 많은 작업이 있으면 재디스패치 (pendingWork_ 변화 없음)
        resumed = workDispatcher_->redispatch(this);
    }
    if(!resumed){
        // 더 이상 작업이 없으면 inFlight 해제
        workDispatcher_->clearInFlight(actor_->id());
    }
    if(hasMoreWork) *hasMoreWork = resumed;
    return r.processed;
}
```

#### 5. `clearInFlight()` — 새 메서드

```cpp
// work_dispatcher.hpp
void clearInFlight(uint64_t actorId);  // onWorkDone() 대체

// work_dispatcher.cpp
// 주의: 이 함수는 배치 처리가 완전히 끝났을 때만 호출됩니다
void WorkDispatcher::clearInFlight(uint64_t actorId){
    inFlight_[actorId % kMaxActors].v.store(0, std::memory_order_release);
    if(pendingWork_.fetch_sub(1, std::memory_order_acq_rel) == 1){
        if(draining_.load(std::memory_order_acquire)){
            for(int i = 0; i < workerCount_; i++) semas_[i]->release();
        }
    }
}
```

> **호출 타이밍**: `run()`에서 `r.hasMoreWork == false`일 때만 호출. 더 많은 작업이 있으면 `redispatch()`로 큐에 다시 넣고 inFlight 상태 유지.

#### 6. `worker.cpp` — `onWorkDone()` 호출 제거

```cpp
// BEFORE (worker.cpp:45)
if(!more) workDispatcher_->onWorkDone();

// AFTER — 제거 (clearInFlight이 내부에서 처리)
// worker는 run만 호출, dedup/카운트는 work_dispatcher 내부에서 관리
```

### 기대 효과

| 항목 | Before | After |
|------|--------|-------|
| enqueue() fence | seq_cst exchange (LOCK XCHG) | 없음 |
| run() fence | seq_cst store + fence + exchange | 없음 |
| seq_cst 연산 수 | 4회/메시지 | 0회 |
| per-actor 아토믹 | `scheduled_` (1개) | `inFlight_` (WorkDispatcher 내부) |
| dedup 비용 | ~60ns (seq_cst exchange + fence) | ~30ns (acq_rel exchange) |

### 검증 항목

1. **기존 단위 테스트 통과** (149개)
2. **throughput 스케일링**: w=4, actors=1→4에서 개선 확인
3. **backpressure**: drain 정상 동작 확인
4. **불변식 검증**: pendingWork_ 카운트 정합성

---

## 4-6. 정확성·신뢰성 하드닝 🟡

> **목표**: 극단 시나리오(홍수·소비자 부재·느린 클라이언트)에서도 메시지/스케줄링/수명주기가 결정적(no hang, no lost wakeup, no silent drop)
>
> **메모**: 2026-08-17 코드베이스 전수 점검에서 발굴. 4-4(로드 밸런싱)가 동시성 경쟁을 겨냥했다면, 4-6은 **잘못된 경로가 조용히 실패하는 지점**을 겨냥한다.

### 4-6.1 백프레셔 계약 (Lost-Wakeup 해소)

> **문제**: 메일박스/디스패처 큐가 가득 차면 조용히 드롭되고, 드롭 시 실행 토큰이 소실되어 액터가 좌초될 수 있음

| 작업 | 상세 | 상태 |
|------|------|------|
| dispatch/redispatch 실패 시 좌초 해소 | `ActorRuntime::enqueue()`/`run()`에서 `dispatch()`/`redispatch()` 실패 시 `scheduled_`만 되돌리고 메시지는 메일박스에 남아 **재스케줄 경로 없음** (`actor_runtime.cpp:42-44,61-72`). 전역 오버플로 큐 또는 워커 스윕(pending 플래그)으로 재스케줄 보장 | ⬜ |
| 메일박스 드롭 시 발신자 통지 | `mailbox_->push` 실패 시 `dropped` 카운터만 증가. NACK/dead-letter 라우팅으로 발신자(또는 감시자)에게 통지하는 옵션 | ⬜ |
| 드롭·좌초 유닛 테스트 | 홍수(> 큐 용량) 시 좌초 없음, 드롭 카운터·재스케줄 보장 검증 | ⬜ |

### 4-6.2 Supervision 후속

> **문제**: 예외 격리는 완료됐지만 복구·관측 경로가 불완전 — dead-letter는 쓰기만, 재시작은 무제한 즉시, 전달 불가 메시지는 조용히 소멸

| 작업 | 상세 | 상태 |
|------|------|------|
| Dead-letter 소비자/관측 | `DeadLetterQueue`(cap 128)는 push만 하고 소비자 없음 (`dead_letter_queue.hpp:6`). 실패 메시지 관측/재시도/로깅 경로 추가 + `deadLetters` 메트릭을 supervisor push와 연동 (`supervisor.cpp:56-66`) | ⬜ |
| 재시작 지연·백오프 | `performRestart`가 즉시 close→open (`actor_runtime.cpp:134-140`). 영구 실패 액터가 배치마다 재시작 → 지수 백오프 + 서킷브레이커 옵션 | ⬜ |
| 전달 불가 sendMsg → dead-letter | 대상 액터 부재/만료 핸들로 `sendMsg`/`sendMsgAfter`/`ActorHandle::send`가 조용히 소멸 (`actor.cpp:13-39`, `actor_handle.cpp:23-28`) → dead-letter 라우팅 옵션 | ⬜ |
| OneForAll 예산·드레인 정합 | OneForAll 브로드캐스트 시 비실패 액터도 restart budget 소진 없이 재시작 (`supervisor.cpp:86-89`), 실패 액터만 메일박스 드레인 (`actor_runtime.cpp:89-97`) → 시맨틱 통일 | ⬜ |

### 4-6.3 비동기 요청 신뢰성 (CLI hang 해소)

> **문제**: 비동기 응답 요청이 타임아웃/conn 식별 없이 단일 필드로 추적 → 소비자 부재·연속 요청 시 hang·응답 유실

| 작업 | 상세 | 상태 |
|------|------|------|
| `pmu status` 행/hang 수정 | `pmuStatusPending_` 단일 bool + `pendingConn_` 단일 필드 (`cmd_actor.cpp:167-169`). device_manager 미동작 시 CLI 무한 대기, conn 2개째 요청 시 첫 클라이언트 응답 유실 → **conn 키 pending 요청 맵 + `sendMsgAfter` 타임아웃/취소** | ⬜ |
| `v2_cli` 수신 타임아웃 | `cli_app.cpp:275-279` recv 루프에 타임아웃 없음 → 데몬 무응답 시 영구 대기. 커넥트/읽기 타임아웃 + 재시도 | ⬜ |

### 4-6.4 논블로킹 전송 (느린 클라이언트 격리)

> **문제**: UDS가 블로킹 `::send` 반복 루프 → 느린/정지 클라이언트가 워커 전체를 정지

| 작업 | 상세 | 상태 |
|------|------|------|
| 비차단 소켓 + write 큐 | `UdsServer::send`/`UdsClient::send` 루프 (`uds_server.cpp:65-79`) → `O_NONBLOCK` + conn별 버퍼 + EPOLLOUT 구동 flush. 워커 스레드가 `::send`에서 블록 금지 | ⬜ |
| `IEventLoop::subscribe` 이벤트 마스크 | `EPOLLIN` 하드코딩 (`event_loop_epoll.cpp:80`), EPOLLOUT/ET/ONESHOT 미사용 → 마스크 파라미터 추가 | ⬜ |
| 전송/워커 격리 테스트 | 느린 소비자 상태에서 나머지 액터 처리량 보존 검증 (integration) | ⬜ |

### 4-6.5 Timer·MemoryPool 후속

> **문제**: 핵심 성능 컴포넌트의 정확성/경계 케이스 결함

| 작업 | 상세 | 상태 |
|------|------|------|
| 반복 타이머 드리프트 재앵커 | `expiry += interval`이 원점 기준 (`timer_base.cpp:106-108`) → 콜백 지연 시 드리프트. `Clock::now()` + interval로 재앵커 + 드리프트 테스트 | ⬜ |
| `cleanupTimerCtxs` O(N) 제거 | `addTimer`마다 `timerCtxs_` 전체 스캔 (`scheduler.cpp:26,41-49`). 지연 GC/오래된 ctx만 정리로 | ⬜ |
| repeating timer clone 비용 | fire마다 `msg.clone()` 힙 할당 (`scheduler.cpp:56`) → 재사용/재배치; non-copyable 메시지가 조용히 빈 메시지 전송 (`message.hpp:81,89`) → 실패 명시화 | ⬜ |
| MemoryPool `kMaxPools` OOB | `poolId_` 무제한 증가 → `assert`(release에서 제거)만 존재, 17번째 풀 시 OOB (`memory_pool.hpp:55,79,98`) → ID bound/가드 + release 검증 | ⬜ |
| poison 정책 완성 | deallocate만 0xCD fill (`memory_pool.hpp:21-26`) → allocate fill + magic 기반 이중 해제 감지 | ⬜ |
| release dealloc size 검증 | `returnBatch` assert (`central_cache.hpp:77,82`) → 잘못된 size/pool 포인터 반환 시 release에서 슬랩 손상. size class 인코딩/검증 추가 | ⬜ |

---

## 4-7. 서비스/구독/설정 정리 🟡

> **목표**: 242a139(데이터 소유자 액터 + pub/sub)의 잔여 결함 정리 + 설정/백엔드 실제 동작과 문서·코드 일치

### 4-7.1 구독 생명주기 + 일반 pub/sub

| 작업 | 상세 | 상태 |
|------|------|------|
| `close()` 시 구독자 정리 | `SystemManagerActor::close()`/`DeviceManagerActor::close()`가 `subscribers_` 미정리 (`system_manager_actor.cpp:40-52`, `device_manager_actor.cpp:25-34`) → 재시작 시 고아 구독자 제거 | ⬜ |
| 죽은 구독자 감지 | 구독자 액터 비활성/재시작 시 데이터 소유자가 계속 publish → 구독자 상태 확인/자동 탈퇴 | ⬜ |
| 일반 pub/sub 추출 | SysData/PmuData/Monitor가 각각 Tick/Subscribe/Unsubscribe/Update 4메시지군을 중복 정의 (`system_manager_messages.hpp`, `device_manager_messages.hpp`, `monitor_messages.hpp`) → 공용 템플릿/토픽 기반 패턴으로 추출, 신규 데이터 소유자(i2c/dbus)가 재사용 | ⬜ |
| 스냅샷 중복 빌드 | `MonitorActor::tryPublish`가 Sys/Pmu 업데이트마다 풀 스냅샷 + 레지스트리 전체 순회 (`monitor_actor.cpp:54-77`) → debounce/데이터 버전(세대) 카운터 | ⬜ |

### 4-7.2 비활성 기능 정리 (wifi / PMU 백엔드)

| 작업 | 상세 | 상태 |
|------|------|------|
| wifi 명령 dead-end 해소 | `network_manager`는 `enable_dbus` 필수 (`main_app.cpp:121-125`) + 결과가 CLI로 미도달 + `NmStatusRequest` 무전송 (`cmd_actor.cpp:189-219`) → async 결과 conn 라우팅 또는 명시적 오류 게이팅. `wifi *`에 대한 README "silently dropped" 문구와 함께 정리 | ⬜ |
| 미사용 메시지 정리 | `DbusRegisterResult`/`DbusIncomingMethodCall`/`DbusProxyCallResult`/`DbusSignalEvent`/`NmStatusRequest` 미생산·미사용 (`message_traits.hpp:28-37`), `TickActor::handle(Tick)` no-op (`tick_actor.cpp:40-41`) → 오디트/제거 | ⬜ |
| PMU 백엔드 런타임 선택 | `__aarch64__` 컴파일타임 고정 (`main_app.cpp:93-102`) → 비-RPi aarch64에서 vcgencmd 9회 스폰 실패. 보드 탐지(`/proc/device-tree/model`)/vcgencmd 존재 검사/우아한 fallback | ⬜ |
| PMU 파서 하드닝 | `std::stof/stoull` 파서가 vcgencmd 이상 출력 시 throw → 액터 핸들러 예외 → supervisor 실패 (`pmu_rsp5.cpp:70-113`). try/catch + 채널별 오류 | ⬜ |

### 4-7.3 설정/코드 정합

| 작업 | 상세 | 상태 |
|------|------|------|
| `enable_pmu` 죽은 키 처리 | config에만 존재, `RuntimeConfig`/로더 미파싱 (`config/v2_main.json:9`) → 백엔드 선택에 연결 또는 키 제거 | ⬜ |
| 앱별 설정 트리밍 | `v2_cli.json`의 `worker_count`/`mailbox_size`/`epoll_max_events` 등 미사용 키, 스케줄러 튜닝 키(`busy_steal_interval_us` 등)가 JSON에 미기재 → 실제 소비 키만 문서화 | ⬜ |
| 설정 검증 | `loadFromFile`이 `catch(...)`로 전부 삼킴 (`json_config_loader.cpp:63`) → 스키마 검증/미지 키 경고/타입 오류 표면화 | ⬜ |
| 데이터 소유자별 poll 간격 분리 | `monitorPollIntervalMs`를 sys/pmu 공유 (`main_app.cpp:111,113`) → `sys_poll_interval_ms`/`pmu_poll_interval_ms` 분리 | ⬜ |
| 액터 이름 상수화 | 하드코딩 문자열 액터명(`"system_manager"` 등) 10+ 사이트 (`main_app.cpp:111-124`, `cmd_actor.cpp`) → 상수/핸들로 오타 시 컴파일 타임 실패 | ⬜ |

---

## Phase 5: 벤치마크 인프라 통일 + 학술 보고서 ⬜

> **목표**: 벤치마크 시스템 단일화 + 학술 수준 성능 분석 + 포트폴리오
>
> ⏳ Phase 4 완료 후 진행 예정

### 벤치마크 인프라 통일

> **문제**: Google Benchmark + 커스텀 벤치마크 이중 체제 → 문서화 비통일, 테스트 환경 2개

| 작업 | 상세 |
|------|------|
| Google Benchmark 제거 | `test/benchmark/` 폴더 + CMake 의존성 제거 |
| 마이크로 벤치마크 추가 | `bench_queue_push.cpp` — push 레이턴시 (기존 `mpsc_queue_bench.cpp` 역할) |
| 마이크로 벤치마크 추가 | `bench_queue_pop.cpp` — pop 레이턴시 |
| 마이크로 벤치마크 추가 | `bench_queue_mpsc.cpp` — 멀티프로듀서 처리량 (기존 `mpsc_queue_multithread_bench.cpp` 역할) |
| 기존 마이크로 벤치마크 이전 | `ring_buffer_bench.cpp`, `timer_bench.cpp` → 커스텀 프레임워크로 재구현 |

### 커스텀 벤치마크 프레임워크 강화

> **목표**: Google Benchmark 수준의 데이터 신뢰성 확보
>
> **현황 점검 (2026-08-17)**: 워밍업/이상치/통계(일부)/cpupower/이력/단일 출력 전부 미구현 — `IBenchmark`는 `name/description/run`만 (`bench/i_benchmark.hpp:8-17`), P50/P95/P99은 latency/scheduler에만, stddev 전무

| 작업 | 상세 | 상태 |
|------|------|------|
| 자동 워밍업 | `IBenchmark::warmupIterations()` 추가, 기본 100회 (현재 CLI arg 기본 0) | ⬜ |
| 이상치 탐지 | 결과에서 이상값 자동 제거 (IQR 방식) | ⬜ |
| 통계 분석 | 평균, 중앙값, P50, P95, P99, 표준편차 — stddev 전 벤치 확장 | ⬜ |
| CPU 클럭 안내 | 실행 전 `cpupower frequency-set -g performance` 안내 | ⬜ |
| 이력 관리 | 이전 결과와 비교하는 `benchmark_history.json` | ⬜ |
| 단일 출력 형식 | 모든 벤치마크가 동일한 JSON/마크다운 출력 생성 (현재 텍스트 전용) | ⬜ |

### 워크스틸링 반영 재측정

> **문제**: `docs/benchmark/*` 수치가 로드 어웨어/워크 스틸링(2026-08-15) 이전 기준 — "single-worker optimal", contention 분석 등 재검증 필요

| 작업 | 상세 | 상태 |
|------|------|------|
| 6개 벤치 재실행 | `throughput`/`latency`/`contention`/`scaling`/`backpressure`/`scheduler` — 워커 1→64 스위프 포함 | ⬜ |
| 수치·결론 갱신 | 다중 워커에서도 균형 유지되는지 확인, 문서 수치/결론 갱신 | ⬜ |
| 백프레셔 문서 재작성 | `backpressure.md`가 구 `MutexMailbox` 구현 스니펫 보유 (`backpressure.md:229-241`) → lock-free 기반으로 재작성 | ⬜ |

### 테스트 커버리지 확대

> **문제**: 143개 테스트 전부 core 전용 — service/transport/config/CLI 레이어 테스트 0건

| 작업 | 상세 | 상태 |
|------|------|------|
| 서비스 레이어 테스트 | MonitorActor pub/sub(구독→수집→발행, 마지막 구독자 해제), CmdActor async pmu, SystemManager/DeviceManager, MonitorBridgeActor | ⬜ |
| 전송 테스트 | `UdsServer`/`UdsClient` — 연결/송수신/EINTR/재연결 | ⬜ |
| 설정 테스트 | `JsonConfigLoader`/`RuntimeConfig::loadFromFile` — 키 파싱/타입 오류/미지 키 | ⬜ |
| CLI·Logger 테스트 | `CliApp` 서브커맨드 파싱, 로그 버퍼 RAII/레벨 필터링 | ⬜ |
| EventLoopEpoll 심화 | 크로스 스레드 구독(pending ops 큐), timerfd, signal-pipe 경로 | ⬜ |

### CI 파이프라인

| 작업 | 상세 | 상태 |
|------|------|------|
| GitHub Actions 워크플로 | push/PR 시 빌드(Release+Debug) + `ctest` + `v2_core_smoke` + 벤치 smoke | ⬜ |
| 캐시/린트 | ccache 캐시, `-Wall -Wextra -Wpedantic` 경고 제로 게이트 | ⬜ |

### 설치/배포 하드닝

| 작업 | 상세 | 상태 |
|------|------|------|
| non-root 실행 | systemd `User=`/샌드박스(`ProtectSystem`, `NoNewPrivileges` 등) (`install.sh:155-176`) | ⬜ |
| 로그 로테이션 | logrotate 규칙 배포 (현재 `log/v2_main.log` 무한 증가) | ⬜ |
| `/etc/v2` 설정 배포 | `V2_CONFIG_DIR`가 repo 소스 참조 (`app.cmake:17,33,55`) → 설치본은 `/etc/v2` 참조(fallback 포함) | ⬜ |
| 소켓 권한 | 모니터/IPC 소켓 0777 (`monitor_bridge_actor.cpp:34`, `ipc_server_actor.cpp:75`) → 서비스 유저 전용 + `SO_PEERCRED`/uid 검사 | ⬜ |
| uninstall 청소 | `/tmp` 소켓/로그/`build/` 제거 + `--purge` 옵션 (`uninstall.sh:1-27`) | ⬜ |
| D-Bus 정책 게이팅 | D-Bus 기본 비활성 상태에서도 정책 설치·재로드 (`install.sh:119-141`) → `enable_dbus`에 연동 | ⬜ |
| CPack 정리 | `CMakeLists.txt:92-94` 주석 처리 → 활성화 또는 제거 + 스테일 빌드 산출물 정리 | ⬜ |

### 학술 보고서

| 작업 | 상세 |
|------|------|
| 반복 측정 | 10회+ 반복, 평균 ± 표준편차, 95% 신뢰구간 |
| 영어 보고서 | Abstract ~ Conclusion, 학술 수준 |
| GitHub 정리 | README, 토픽, 데모 자료 (GIF/스크린샷) |

---
