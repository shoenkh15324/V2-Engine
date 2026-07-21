# V2-Engine 로드맵

---

## 일정

```
Phase 1: 성능 병목 제거 ✅ 완료
Phase 2: actor_system 리팩토링 🔄 진행 중
Phase 3: 메모리/전송 최적화
Phase 4: 아키텍처 고도화
Phase 5: 벤치마크 인프라 + 보고서
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

**변경 파일**: `lock_free_mpsc_queue.hpp`, `actor_system.hpp/cpp`, `actor_runtime.hpp/cpp`, `work_dispatcher.hpp/cpp`, `worker.cpp`, 테스트/벤치마크

---

## Phase 2: actor_system 리팩토링

> **목표**: 강결합 구조 해소 → Runtime과 Actor 완전 분리, 단방향 의존성, 컴파일 의존성 최소화, 확장 가능한 구조 확보
> 
> **진행 상황**:
> - ✅ `IActorRuntime` 인터페이스 도입 (Actor ↔ ActorContext 순환 의존 제거)
> - ✅ Dispatcher를 WorkDispatcher / EventLoopEpoll / Scheduler로 분리
> - ✅ `IWorkDispatcher`, `IEventLoop` 인터페이스 도입 (의존성 역전)
> - ✅ `ActorContext` → `ActorRuntime` 리네임
> - ✅ `ActorHandle` 도입 (generation 기반 safe reference, dangling 방지)
> - ✅ Registry를 Lookup 전용으로 축소 + enableActor/disableActor를 Runtime으로 이동
> - ✅ Registry 런타임 시 lock-free 구조 완성
> - 🔄 PImpl 적용으로 컴파일 의존성 최소화

### 디렉토리 구조

```
actor_system/
    ├── actor_system.hpp           # Facade
    ├── actor_system.cpp
    │
    ├── actor/
    │   ├── actor.hpp              # Actor 추상 클래스
    │   ├── actor.cpp
    │   ├── actor_handle.hpp       # generation 기반 safe reference
    │   └── actor_handle.cpp
    │
    ├── runtime/
    │   ├── i_actor_runtime.hpp    # Actor가 의존할 인터페이스
    │   ├── actor_runtime.hpp      # IActorRuntime 구현체
    │   ├── actor_runtime.cpp
    │   ├── i_scheduler.hpp        # Scheduler 인터페이스
    │   ├── scheduler.hpp
    │   ├── scheduler.cpp
    │   ├── i_actor_registry.hpp   # Registry 인터페이스
    │   ├── actor_registry.hpp
    │   ├── actor_registry.cpp
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
    └── message/
        ├── message.hpp
        └── type_id.hpp
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
// IActorRegistry — 순수 lookup
class IActorRegistry {
    virtual ActorHandle findByName(const std::string& name) = 0;
    virtual ActorHandle findById(uint64_t id) = 0;
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

#### 4. PImpl 적용으로 컴파일 의존성 최소화

**현재 문제**: `actor_system.hpp`가 모든 구현체를 include

**개선**: 헤더에 인터페이스만 노출

```cpp
// actor_system.hpp
class ActorSystem {
public:
    ActorSystem();
    ~ActorSystem();
    
    template<typename T, typename... Args>
    ActorId spawn(Args&&... args);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

#### 5. MessageEnvelope 기반 메시지 시스템

**현재 문제**: `std::variant<Msg1, Msg2, ..., Msg28>` — 모든 메시지 헤더가 컴파일됨

**개선**: Runtime은 전달만 담당, 메시지 내용은 각 모듈이 정의

```cpp
// message/envelope.hpp
struct MessageEnvelope {
    ActorId sender;
    ActorId receiver;
    uint16_t typeId;
    std::shared_ptr<void> payload;
    
    template<typename T>
    static MessageEnvelope create(ActorId sender, ActorId receiver, T&& data);
    
    template<typename T>
    const T* as() const;
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

### 리팩토링 우선순위

| 순위 | 작업 | 상태 | 기간 |
|------|------|------|------|
| 1 | `IActorRuntime` 인터페이스 도입 | ✅ 완료 | 2-3일 |
| 2 | Dispatcher를 WorkDispatcher / EventLoopEpoll / Scheduler로 분리 | ✅ 완료 | 5일 |
| 3 | MessageEnvelope 기반 메시지 시스템 구축 | | 3-4일 |
| 4 | Registry를 Lookup 전용으로 축소 + ActorHandle 도입 | ✅ 완료 | 1-2일 |
| 5 | ActorSystem이 Actor 생명주기를 단독 관리 | | 2-3일 |
| 6 | PImpl을 적용하여 컴파일 의존성 최소화 | | 1-2일 |
| 7 | Runtime API를 최소화하여 Actor와 Runtime 완전 분리 | | 2-3일 |

**총 예상 기간: 14-20일** (Phase 2 완료로 9일 단축)

### 변경 파일

**Phase 2 완료 파일:**
- **신규**: `i_work_dispatcher.hpp`, `work_dispatcher.hpp/cpp`, `i_event_loop.hpp`, `event_loop_epoll.hpp/cpp`, `i_actor_runtime.hpp`, `i_scheduler.hpp`, `i_actor_registry.hpp`, `actor_handle.hpp/cpp`
- **수정**: `actor_system.hpp/cpp`, `actor_runtime.hpp/cpp`, `worker.hpp/cpp`, `scheduler.hpp/cpp`, `actor.hpp`, `actor_registry.hpp/cpp`, `cmd_actor.cpp`, `monitor_actor.cpp`, `network_manager.cpp`, `core.cmake`
- **삭제**: `dispatcher.hpp/cpp`, `i_io_event_loop.hpp`, `io_event_loop.hpp/cpp`, `worker_pool.hpp/cpp`

**Phase 3~5 변경 파일:**
`actor.hpp/cpp`, `actor_runtime.hpp`, `dispatcher.hpp/cpp`, `worker.hpp/cpp`, `actor_system.hpp/cpp`, `scheduler.cpp`, `timer.hpp/cpp`, `lock_free_mpsc_queue.hpp`, `metrics.hpp/cpp`, `log.cpp`

---

## Phase 3: 메모리/전송 최적화

> **목표**: 핫 패스 캐시 미스 + 불필요한 할당/잠금/원자 연산 제거

### 메시지 전송 경로 최적화

> **문제**: `sendMsg()` 호출당 `ActorRegistry` 뮤텍스 + `unordered_map` 조회 → 매 메시지마다 futex + 해시 체인 순회

| 작업 | 상세 |
|------|------|
| 생성자 문자열 이동 | `Actor::Actor(name)` → `name_(std::move(name))` 불필요한 복사 제거 |

### 캐시 라인 패딩

> **문제**: 빈번한 원자적 접근 필드가 인접 필드와 같은 캐시 라인에 위치 → false sharing

| 작업 | 상세 |
|------|------|
| `ActorRuntime::scheduled_` | 프로듀서 쓰기 / 워커 읽기 간 캐시 라인 분리 (`alignas(64)`) |
| `ActorMetrics` | 6개 atomic을 48바이트에 패킹 → `alignas(64)`로 분리 (`metrics.hpp:8-14`) |
| `WorkerMetrics` / `DispatcherMetrics` | 인접 atomic 패딩 추가 (`metrics.hpp:46-80`) |
| `kCacheLine` 상수 공유 | `lock_free_mpsc_queue.hpp` → `common/`으로 이동, 전역 사용 |

### 타이머 메모리 할당 제거

> **문제**: `Scheduler::addTimer()` 호출당 3~4회 힙 할당 (`shared_ptr<Message>`, 람다 캡처, `TimerNode`, `std::function`)

| 작업 | 상세 |
|------|------|
| `shared_ptr<TimerNode>` 제거 | `TimerNode`를 풀 할당 또는 스택 배치로 변경 |
| `shared_ptr<Message>` 제거 | `Scheduler::addTimer()`에서 Message를 `TimerNode` 내에 직접 저장 |
| `std::function` 제거 | `Timer::Callback`을 직접 호출 가능한 타입으로 변경 (함수 포인터 + void* 또는 CRTP) |

### 메시지 복사 비용

> **문제**: `sizeof(Message)` = **~208바이트** (기존 추정 120-160) — MPSC 큐 push 시 매번 inline 이동

| 작업 | 상세 |
|------|------|
| Message 크기 확인 | `sizeof(Message)` = 208바이트 확인 (`DbusProxyCallRequest` 7개 `std::string` 기준) |
| Pimpl 또는 힙 할당 | 큰 variant는 `unique_ptr`로 감싸서 push 시 포인터 복사만 |

### 전역 로깅 뮤텍스 제거

> **문제**: `logPrint()`가 모든 워커 스레드를 하나의 `gMutex`로 직렬화

| 작업 | 상세 |
|------|------|
| Lock-free 로깅 | `log.cpp`의 전역 `std::mutex` → 락프리 링 버퍼 + 전용 라이터 스레드 또는 per-thread 로깅 |

### 메모리 순서 최적화

> **문제**: 핫 패스 원자들이 기본 `seq_cst` 사용 → ARM에서 불필요한 `DMB ISH` 배리어

| 작업 | 상세 |
|------|------|
| `scheduled_` | `exchange(true)` → `memory_order_acq_rel`, `store(false)` → `memory_order_release` |
| `running_` 플래그 | `Dispatcher::running_`, `Worker::running_` → load는 `relaxed`, store는 `release` |

### 메트릭 핫 패스 최적화

> **문제**: `count()`가 metrics 활성화 여부와 무관하게 매 메시지마다 두 번의 atomic 로드 수행

| 작업 | 상세 |
|------|------|
| 지연 `count()` | `Metrics::recordDispatch/recordEnqueue` 내부에서만 `count()` 호출하도록 변경 |
| 메트릭 비활성화 시 zero-overhead | `isEnabled()` 체크를 호출 전으로 이동, 비활성화 시 atomic 로드 0회 |

**변경 파일**: `actor.hpp/cpp`, `actor_runtime.hpp/cpp`, `work_dispatcher.hpp/cpp`, `worker.hpp`, `timer.hpp/cpp`, `scheduler.cpp`, `lock_free_mpsc_queue.hpp`, `metrics.hpp/cpp`, `log.cpp`

---

## Phase 4: 아키텍처 고도화

> **목표**: 정확성 + 타입 안전 + 장애 처리 + 로드 밸런싱

### 타입별 메시지 디스패치

> **문제**: `std::variant<MessageTypes...>` 단일 블롭 — 모든 액터가 모든 메시지 타입을 받음, `[](const auto&){}`로 미처리 타입 무시

| 작업 | 상세 |
|------|------|
| `typed_channel.hpp` | 액터별 수신 가능 타입을 컴파일 타임에 제한하는 타입 안전 채널 |
| `handle()` 분리 | 기존 `handle(const Message&)` → `handle(const SpecificMsg&)` 오버로드로 변경 |
| variant 분할 | `Message`를 서브 시스템별 variant로 분리 (IPC, D-Bus, Device, Cmd, Network) |
| 데드 메시지 처리 | 미처리 타입 로깅 + 메트릭 (`dead_letter` 카운터), `DbusRegisterResult` 등 미사용 타입 정리 |

### Supervision 트리 + 예외 격리

> **문제**: `handle()` 예외 시 워커 스레드 크래프 → 프로세스 전체 종료, 복구 불가

| 작업 | 상세 |
|------|------|
| `supervisor.hpp` | 액터 실패 처리/재시작 트리 구조 |
| 예외 격리 | `ActorRuntime::run()`에서 `try/catch`로 `handle()` 감싸기, 크래프된 액터 격리 |
| 재시작 전략 | `one_for_one` (단일 액터 재시작), `one_for_all` (부모-자식 전체 재시작) |
| 데드 레터 큐 | 실패한 메시지를 보관하는 큐 (디버깅/재시도용) |
| 라이프사이클 훅 | `preStart()`, `postStop()`, `preRestart()`, `postRestart()` 추가 |

### Work Stealing

> **문제**: 고정 악피니티 — 워커 0에 액터 10개, 워커 1에 0개 → 불균형, 워커 1 유휴

| 작업 | 상세 |
|------|------|
| `work_stealing_queue.hpp` | 각 워커의 로컬 큐에서 다른 워커가 작업을 훔치는 구조 |
| 유휴 감지 | 워커가 `acquire()` 타임아웃 시 다른 워커 큐에서 steal 시도 |
| 로드 밸런싱 메트릭 | 워커별 큐 깊이/처리량 모니터링, 임계 초과 시 자동 리밸런싱 |

### 정확성 버그 수정

> **문제**: 동시성 레이스 + 수명 주기 안전성 결여

| 작업 | 상세 |
|------|------|
| `scheduled_` 더블 디스패치 | `scheduled_=false` → `empty()` 사이 프로듀서가 중복 디스패치 가능 → `exchange` 기반으로 수정 (`actor_runtime.cpp:45-47`) |
| 타이머 use-after-free | `Scheduler::addTimer()`에서 `Actor*` raw 포인터 캡처 → `ActorRuntime*` 또는 `weak_ptr`로 변경 (`scheduler.cpp:13-14`) |
| `ActorState` 레이스 | non-atomic `uint8_t`를 메인/워커 스레드가 동시에 접근 → `std::atomic<uint8_t>` 또는 `std::atomic<ActorState>` (`actor.hpp:53`) |
| 그레이셔널 드레인 | `ActorSystem::stop()` 시 미처리 메시지 처리 완료 후 중지 → 드레인 단계 추가 (`actor_system.cpp:30-38`) |
| `Worker::stop()` 데드락 | 세마포어 해제 없이 `join()` 호출 시 데드락 → 정지 순서 강제 또는 세마포어 추가 해제 (`worker.cpp:22-27`) |

**변경 파일**: `typed_channel.hpp` (신규), `supervisor.hpp` (신규), `work_stealing_queue.hpp` (신규), `actor.hpp/cpp`, `actor_runtime.hpp/cpp`, `work_dispatcher.hpp/cpp`, `worker.hpp/cpp`, `actor_system.hpp/cpp`, `scheduler.cpp`

---

## Phase 5: 벤치마크 인프라 통일 + 학술 보고서

> **목표**: 벤치마크 시스템 단일화 + 학술 수준 성능 분석 + 포트폴리오

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
