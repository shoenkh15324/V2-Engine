# V2-Engine 로드맵

---

## 일정

```
Phase 1: 성능 병목 제거 ✅ 완료
Phase 2: actor_system 리팩토링 ✅ 완료
Phase 3: 메모리/전송 최적화 ✅ 완료
Phase 4: 아키텍처 고도화 🔄 진행 중 
Phase 5: 벤치마크 인프라 + 보고서 ⬜ 대기 
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
| `MessageId` enum ✅ | 33개 메시지 타입 ID 정의 (`message_traits.hpp`) |
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
> **진행 순서**: Supervision → 정확성 버그 → typed_channel → Work Stealing

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

### 4-3. 타입별 메시지 디스패치 🔜

> **문제**: `Message`가 단일 타입 — 모든 액터가 모든 메시지 타입을 받음, 런타임 `switch`로 식별

| 작업 | 상세 |
|------|------|
| `typed_channel.hpp` | 액터별 수신 가능 타입을 컴파일 타임에 제한하는 타입 안전 채널 |
| `handle()` 분리 | 기존 `handle(const Message&)` → `handle(const SpecificMsg&)` 오버로드로 변경 |
| 데드 메시지 처리 | 미처리 타입 로깅 + 메트릭 (`dead_letter` 카운터), 미사용 타입 정리 |

### 4-4. Work Stealing 🔜

> **문제**: 고정 악피니티 — 워커 0에 액터 10개, 워커 1에 0개 → 불균형, 워커 1 유휴

| 작업 | 상세 |
|------|------|
| `work_stealing_queue.hpp` | 각 워커의 로컬 큐에서 다른 워커가 작업을 훔치는 구조 |
| 유휴 감지 | 워커가 `acquire()` 타임아웃 시 다른 워커 큐에서 steal 시도 |
| 로드 밸런싱 메트릭 | 워커별 큐 깊이/처리량 모니터링, 임계 초과 시 자동 리밸런싱 |

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

| 작업 | 상세 |
|------|------|
| 자동 워밍업 | `IBenchmark::warmupIterations()` 추가, 기본 100회 |
| 이상치 탐지 | 결과에서 이상값 자동 제거 (IQR 방식) |
| 통계 분석 | 평균, 중앙값, P50, P95, P99, 표준편차 |
| CPU 클럭 안내 | 실행 전 `cpupower frequency-set -g performance` 안내 |
| 이력 관리 | 이전 결과와 비교하는 `benchmark_history.json` |
| 단일 출력 형식 | 모든 벤치마크가 동일한 JSON/마크다운 출력 생성 |

### 학술 보고서

| 작업 | 상세 |
|------|------|
| 반복 측정 | 10회+ 반복, 평균 ± 표준편차, 95% 신뢰구간 |
| 영어 보고서 | Abstract ~ Conclusion, 학술 수준 |
| GitHub 정리 | README, 토픽, 데모 자료 (GIF/스크린샷) |

---
