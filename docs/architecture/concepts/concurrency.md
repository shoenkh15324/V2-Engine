# 동시성

---

## 목차

- [요약 (세 문장)](#요약-세-문장)
- [개요](#개요)
- [메모리 오더링 입문 (표를 읽기 전에)](#메모리-오더링-입문-표를-읽기-전에)
- [설계 원칙](#설계-원칙)
- [락프리 데이터 구조](#락프리-데이터-구조)
  - [MPSC 큐](#mpsc-큐-멀티-프로듀서-단일-소비자)
  - [MPMC 큐](#mpmc-큐-멀티-프로듀서-멀티-소비자)
  - [캐시 라인 인식](#캐시-라인-인식)
- [스레딩 모델](#스레딩-모델)
  - [워커 스레드](#워커-스레드)
  - [세마포어 기반 웨이크업](#세마포어-기반-웨이크업)
  - [뮤텍스 사용 경계](#뮤텍스-사용-경계)
- [작업 분배](#작업-분배)
  - [액터 친화성](#액터-친화성)
  - [부하 인식 디스패치](#부하-인식-디스패치)
  - [작업 스틸링](#작업-스틸링)
  - [적응형 백오프](#적응형-백오프)
  - [드레인 프로토콜](#드레인-프로토콜)
- [액터 스레드 안전성](#액터-스레드-안전성)
  - [단일 실행 보장](#단일-실행-보장)
  - [In-Flight 슬롯 (Deduplication Gate)](#inflight-슬롯-deduplication-gate)
  - [재시작 카운터 CAS 루프](#재시작-카운터-cas-루프)
  - [중지 플래그](#중지-플래그)
- [메모리 오더링 참조](#메모리-오더링-참조)
- [이벤트 루프 통합](#이벤트-루프-통합)
  - [스레드 간 포스팅](#스레드-간-포스팅)
  - [스레드 친화성 인식](#스레드-친화성-인식)
- [스레드 로컬 저장소](#스레드-로컬-저장소)
  - [TCMalloc 스타일 메모리 캐시](#tcmalloc-스타일-메모리-캐시)
  - [워커 전용 백오프 상태](#워커-전용-백오프-상태)
- [백프레셔 & 흐름 제어](#백프레셔--흐름-제어)
- [요약](#요약)

---

## 요약 (세 문장)

1. 메시지 경로(큐잉 → 디스패치 → 획득 → 처리)에는 **락이 전혀 없습니다** — 락프리 큐와 원자 연산만 사용합니다.
2. 각 액터는 **한 번에 한 워커에서만** 실행됩니다 — 메일박스가 단일 소비자 큐이고 실행 토큰이 중복 발행을 막기 때문입니다.
3. 놀고 있는 워커는 **스핀 → 세마포어 대기 → 작업 스틸링** 순서로 일거리를 찾습니다 — 깨우기 비용을 줄이면서 동시에 놀지도 않습니다.

---

## 개요

> 📌 실행 토큰 생명주기·inFlight 슬롯·finalize 정산·스핀 티어의 상세 설계와 도입 배경은 [작업 분배](work_dispatch.md) 문서를 참고하세요. 이 문서는 동시성 프리미티브 관점을 다룹니다.

V² Engine은 세 가지 기둥 위에 구축된 액터 기반 동시성 모델을 사용합니다: **락프리 데이터 구조**, **작업 스틸링 워커 풀**, **액터 친화성**. 스레드 안전성은 공유 상태 락이 아닌 락프리 큐를 통한 메시지 전달을 통해 달성됩니다. 락은 메시지 핫 패스 외부의 인프라 구성 요소로 제한됩니다.

---

## 메모리 오더링 입문 (표를 읽기 전에)

이 문서 뒤쪽 표들에 `relaxed`, `acquire`, `release` 같은 용어가 계속 나옵니다. 처음이라면 이 절만 읽고 넘어와도 표를 해석할 수 있습니다.

**원자 변수(`std::atomic`)란?** 여러 스레드가 동시에 건드려도 연산이 잘리지 않는 변수입니다. 평범한 `count_++`는 "읽기 → 더하기 → 쓰기" 세 단계 사이에 다른 스레드가 끼어들 수 있지만, 원자 변수의 연산은 한 덩어리로 처리됩니다.

그런데 원자 변수에도 남는 문제가 하나 있습니다 — **순서**입니다. 스레드 A가 "데이터를 쓴 *뒤에* 플래그를 올렸다"고 해도, 스레드 B가 플래그를 보고 데이터를 읽으면 아직 옛날 값일 수 있습니다. CPU와 컴파일러가 성능을 위해 명령을 재배치하기 때문입니다. **메모리 오더링**은 "이 순서만큼은 지켜달라"는 약속의 강도를 고르는 것입니다:

| 오더링 | 강도 | 순한 비유 |
|--------|------|-----------|
| `relaxed` | 약함 — 값 자체만 안전 | 혼자 채우는 **카운터** — 몇 번인지는 정확하지만, 다른 데이터와의 순서는 보장 안 함 |
| `release`(쓰기) + `acquire`(읽기) | 중간 — **짝**으로 동작 | **택배 발송**: 발신자가 박스를 봉인(release)하고 송장을 붙임 → 수신자가 송장을 확인(acquire)하면 박스 안 내용물이 *전부* 보임 |
| `acq_rel` | 중간 — 교환 연산용 | exchange처럼 읽기+쓰기를 동시에 할 때 위 두 역할을 한 번에 수행 |
| `seq_cst` | 강함 — 전역 일관 순서 | 번호표 시스템 — 모든 스레드가 완전히 같은 순서를 봄. 가장 정확하고 가장 비쌈 |

V² Engine의 선택 기준은 단순합니다: **증명 가능한 한 가장 싼 오더링을 쓴다.**

- 그냥 세기만 하면 → `relaxed`
- "내가 쓴 데이터를 상대에게 보여준다"(큐 슬롯 게시 등) → `release` 쓰기 + `acquire` 읽기
- 슬롯 소유권을 원자적으로 교환(RMW) → `acq_rel`

뒤쪽의 [메모리 오더링 참조](#메모리-오더링-참조) 표는 "각 위치에서 어떤 등급을 골랐는지, 왜 충분한지"의 목록입니다. 마지막으로 **CAS**(`compare_exchange`)는 "값이 예상과 같으면 바꾸고, 아니면 실패 보고"하는 원자 조건부 교환으로, 재시도 루프와 함께 쓰면 "경쟁자가 여러 있어도 딱 한 명만 이긴다" 로직이 됩니다.

---

## 설계 원칙

| 원칙 | 설명 |
|------|------|
| **핫 패스 락프리** | 메시지 큐잉, 디스패치, 획득, 배치 처리에 원자와 락프리 큐만 사용. 크리티컬 패스에 뮤텍스 없음. |
| **단일 실행 보장** | 액터의 `handle()`은 정확히 하나의 워커에 의해 호출됨. 액터 수준 락 불필요. |
| **협력 스케줄링** | 워커는 설정 가능한 배치(기본값: 32)로 메시지 처리 후 양보하여 공정성 보장. |
| **락 경계** | 뮤텍스는 빈번하지 않은 경로(타이머 등록, 레지스트리 쓰기, 폴백 큐)와 인프라(메모리 할당자, 로거)로 제한됨. |
| **제로카피 메시지 전달** | 메시지는 락프리 큐를 통해 복사되지 않고 이동됨. SBO 최적화로 작은 메시지에 힙 할당 없음. |

---

## 락프리 데이터 구조

### MPSC 큐 (멀티 프로듀서, 단일 소비자)

**파일:** `src/core/common/container/lock_free_mpsc_queue.hpp`

Dmitry Vyukov의 시퀀스-락 링 버퍼. 액터 메일박스, 데드 레터 큐, 스레드 간 이벤트 포스팅에 사용됩니다.

**슬롯 레이아웃:**

```
Slot {
    std::atomic<size_t> sequence;   // ownership token
    alignas(T) std::byte storage[sizeof(T)];  // inline element storage
}
```

**Push (모든 스레드):**

```cpp
bool push(T&& msg) noexcept {
    size_t pos = tail_.value.load(std::memory_order_relaxed);
    for(;;){
        Slot& slot = slots_[pos % capacity_];
        size_t seq = slot.sequence.load(std::memory_order_acquire);  // (1)
        auto diff = static_cast<SignedSize>(seq) - static_cast<SignedSize>(pos);
        if(diff == 0){
            if(tail_.value.compare_exchange_weak(pos, pos + 1,       // (2)
                std::memory_order_relaxed, std::memory_order_relaxed)){
                ::new (static_cast<void*>(slot.storage)) T(std::move(msg));
                slot.sequence.store(pos + 1, std::memory_order_release);  // (3)
                return true;
            }
        }else if(diff < 0){
            return false;  // queue full
        }else{
            pos = tail_.value.load(std::memory_order_relaxed);  // retry
        }
    }
}
```

| 단계 | 연산 | 메모리 오더링 | 근거 |
|------|------|---------------|------|
| (1) | `slot.sequence` 읽기 | `acquire` | 이 슬롯의 최신 상태를 봄 |
| (2) | `tail_`에 CAS | `relaxed` | 경쟁 해결만; 데이터 의존성 없음 |
| (3) | `slot.sequence` 쓰기 | `release` | placement-new를 소비자에게 게시 |

**Pop (단일 소비자 전용):**

```cpp
bool pop(T& out) noexcept {
    size_t pos = head_.value.load(std::memory_order_relaxed);
    Slot& slot = slots_[pos % capacity_];
    size_t seq = slot.sequence.load(std::memory_order_acquire);  // (4)
    auto diff = static_cast<SignedSize>(seq) - static_cast<SignedSize>(pos + 1);
    if(diff != 0) return false;

    T* element = slot.element();
    out = std::move(*element);
    element->~T();

    head_.value.store(pos + 1, std::memory_order_relaxed);       // (5)
    slot.sequence.store(pos + capacity_, std::memory_order_release);  // (6)
    return true;
}
```

| 단계 | 연산 | 메모리 오더링 | 근거 |
|------|------|---------------|------|
| (4) | `slot.sequence` 읽기 | `acquire` | 게시된 요소를 읽음 |
| (5) | `head_` 쓰기 | `relaxed` | 안전 — 단일 소비자, 데이터 의존성 없음 |
| (6) | `slot.sequence` 쓰기 | `release` | 프로듀서를 위해 슬롯 재활용 |

**사용 위치:**
- `Mailbox` — 모든 액터의 메일박스
- `DeadLetterQueue` — 결함 메시지
- `EventLoopEpoll::pendingOps_` — 스레드 간 이벤트 포스팅

---

### MPMC 큐 (멀티 프로듀서, 멀티 소비자)

**파일:** `src/core/common/container/lock_free_mpmc_queue.hpp`

Dmitry Vyukov의 유계 MPMC 큐. `WorkDispatcher` 워커별 준비 큐에 사용됩니다(작업 스틸링에는 여러 소비자가 필요).

**MPSC와의 핵심 차이:** `pop()`은 여러 워커가 같은 큐에서 동시에 디큐할 수 있으므로 `head_`에 CAS를 사용합니다.

```cpp
bool pop(T& out) noexcept {
    size_t pos = head_.value.load(std::memory_order_relaxed);
    for(;;){
        Slot& slot = slots_[pos % capacity_];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        auto diff = static_cast<SignedSize>(seq) - static_cast<SignedSize>(pos + 1);
        if(diff == 0){
            if(head_.value.compare_exchange_weak(pos, pos + 1,     // CAS (not just store)
                std::memory_order_relaxed)){
                T* element = slot.element();
                out = std::move(*element);
                element->~T();
                slot.sequence.store(pos + capacity_, std::memory_order_release);
                return true;
            }
        }else if(diff < 0){
            return false;
        }else{
            pos = head_.value.load(std::memory_order_relaxed);
        }
    }
}
```

**사용 위치:** `WorkDispatcher` — 워커당 1개, 워커 간 스틸링 가능.

---

### 캐시 라인 인식

**파일:** `src/core/common/container/cache_line.hpp`

```cpp
#if defined(__APPLE__) && defined(__arm64__)
    inline constexpr size_t kCacheLine = 128;
#elif defined(__aarch64__)
    inline constexpr size_t kCacheLine = 64;
#else
    inline constexpr size_t kCacheLine = 64;
#endif
```

모든 큐의 `head_`/`tail_` 원자와 성능 메트릭 카운터는 같은 캐시 라인에 있는 인접 데이터 구조 간 **거짓 공유**를 방지하기 위해 `kCacheLine`으로 정렬됩니다.

```cpp
struct alignas(kCacheLine) AlignedAtomic {
    std::atomic<size_t> value{0};
};
```

---

## 스레딩 모델

### 워커 스레드

**파일:** `src/core/actor_system/runtime/dispatcher/worker.hpp`, `worker.cpp`

각 `Worker`는 `std::thread`와 `std::atomic<bool> running_`을 소유합니다.

**워커 루프:**

```cpp
void Worker::runLoop() {
    // running_이 꺼져도 드레인 중이면 남은 작업을 마저 처리한다
    while(running_.load(std::memory_order_relaxed) || workDispatcher_->isDraining()) {
        auto idleStartTime = Time::now();
        ActorRuntime* actorRuntime = workDispatcher_->acquire(id_);   // 스핀 → 세마포어 / 스틸
        auto idleEndTime = Time::now();

        if(!actorRuntime) {
            workDispatcher_->drainPendedActor();                      // 폴백 큐 재시도
            if(workDispatcher_->isDraining() && (workDispatcher_->pendingWork() == 0))
                break;                                                // 남은 일 없음 → 종료
            if(!running_.load(std::memory_order_relaxed))
                break;
            continue;
        }

        auto busyStartTime = Time::now();
        int processed = actorRuntime->run(maxBatch_);                 // 메시지 배치 처리
        auto busyEndTime = Time::now();                               // (run() 내부 finalize()가 토큰 정산/회계까지 전담)

        uint64_t gapIdleNs = Time::toNs(idleEndTime - idleStartTime);
        uint64_t gapBusyNs = Time::toNs(busyEndTime - busyStartTime);
        V2_METRICS()->recordBatch(id_, processed, gapBusyNs, gapIdleNs);  // 유휴/활성 시간 기록
    }
}
```

핵심 특성:
- 워커는 유휴 시 `std::counting_semaphore`에서 블로킹되지만, 시스템 콜 없이 짧은 간극을 흡수하는 유계 스핀 윈도우(`parkSpinNs`, 기본 3μs) **이후에만** 차단됨 (스핀-던-파크, [작업 분배 §7](work_dispatch.md))
- 배치 처리(`maxBatch=32`)로 디스패치 오버헤드를 경감
- 협력 스케줄링 — 액터가 배치 후 양보
- 메트릭이 워커별 유휴/활성 시간을 추적

---

### 세마포어 기반 웨이크업

**파일:** `src/core/actor_system/runtime/dispatcher/work_dispatcher.hpp`

`WorkDispatcher`는 워커별 `std::counting_semaphore<>` 1개를 보유합니다:

| 연산 | 위치 | 효과 |
|------|------|------|
| `semas_[workerId]->release()` | `enqueueEntry()` | 새 작업이 디스패치될 때 대상 워커를 깨움 |
| `semas_[workerId]->try_acquire_for()` | `acquire()` | 워커가 타임아웃과 함께 슬리프, 시그널이나 타임아웃 시 깨어남 — **스핀 티어가 먼저 실행** |
| `semas_[victim]->try_acquire()` | `trySteal()` | 다른 워커 큐에서 팝할 때 세마포어 토큰을 스틸 |
| `semas_[i]->release()` (전체) | `beginDrain()`, `retirePendingWork()` | 종료를 위해 모든 워커를 깨움 |

---

### 뮤텍스 사용 경계

락은 **빈번하지 않거나 폴백** 경로로 제한됩니다:

| 뮤텍스 | 위치 | 목적 | 핫 패스? |
|--------|------|------|----------|
| `WorkDispatcher::mutex_` | `work_dispatcher.hpp:55` | `pendingActorList_` 폴백 큐 보호 | 아니오 — 락프리 push 실패 시에만 |
| `ActorRuntime::timerMutex_` | `actor_runtime.hpp:67` | `timerIds_` 집합 보호 | 아니오 — 타이머 추가/취소는 빈번하지 않음 |
| `Scheduler::mutex_` | `scheduler.hpp:33` | 타이머 등록 맵 보호 | 아니오 — 타이머 연산 |
| `Supervisor::mutex_` | `supervisor.hpp:62` | 액터별 전략 오버라이드 보호 | 아니오 — 정책 조회 |
| `CentralCache::mutex_` | `central_cache.hpp:115` | 슬래브 할당/해제 보호 | 빈번하지 않음 — 배치 리필 |
| `ActorRegistry::mutex_` | `actor_registry.hpp:38` | `shared_mutex`로 액터 조회/추가/제거 | 읽기: 공유; 쓰기: 독점 |
| `Logger::mutex_` | `log.cpp:34` | 로그 출력 파일 I/O 보호 | 아니오 — 배치 쓰기 |
| `EventLoopEpoll::handlersMutex_` | `event_loop_epoll.hpp:36` | fd-to-handler 맵 보호 | 아니오 — 구독/구독 해제만 |

**메시지 핫 패스(큐잉 → 디스패치 → 획득 → run → handle)는 완전히 락프리입니다.**

---

## 작업 분배

### 액터 친화성

각 액터는 결정적으로 "홈" 워커에 할당됩니다:

```cpp
int WorkDispatcher::pickWorker(uint64_t actorId) {
    int home = static_cast<int>(actorId % workerCount_);
    if(workerCount_ <= 1) return home;
    if(queues_[home]->count() < static_cast<size_t>(highWatermark_)) return home;
    return pickLeastLoaded(actorId);
}
```

이를 통해:
- **캐시 유연성** — 같은 액터의 메시지가 같은 워커에 의해 처리됨 (따뜻한 L1/L2)
- **최소 경쟁** — 홈 워커만 MPSC 메일박스에서 팝; 어떤 스레드든 push 가능

---

### 부하 인식 디스패치

홈 워커의 큐가 **하이 워터마크**(큐 용량의 70%)를 초과하면, 디스패처는 **최소 부하 워커**로 라우팅합니다:

```cpp
int WorkDispatcher::pickLeastLoaded(uint64_t actorId) {
    int best = static_cast<int>(actorId % workerCount_);
    size_t bestCount = queues_[best]->count();
    for(int i = 0; i < workerCount_; i++) {
        int w = static_cast<int>((best + i) % workerCount_);
        size_t c = queues_[w]->count();
        if(c < bestCount) {
            best = w;
            bestCount = c;
            if(bestCount == 0) break;
        }
    }
    return best;
}
```

선택된 큐가 가득 차면 `ActorRuntime*`은 `pendingActorList_`(뮤텍스 보호)로 폴백되고, 워커 유휴 시간에 `drainPendedActor()`를 통해 재시도됩니다.

---

### 작업 스틸링

워커의 자체 큐가 비어있고 세마포어를 통해 작업을 획득할 수 없으면, 인접 워커로부터 **스틸**합니다:

```cpp
bool WorkDispatcher::trySteal(int workerId, ActorRuntime*& out) {
    for(int i = 1; i < workerCount_; i++) {
        int victim = (workerId + i) % workerCount_;
        if(queues_[victim]->empty()) continue;
        if(queues_[victim]->pop(out)) {
            semas_[victim]->try_acquire();  // consume the semaphore token
            return true;
        }
    }
    return false;
}
```

스틸은 다른 모든 워커의 MPMC 큐를 순회합니다. MPMC 큐는 여러 소비자를 허용하므로 추가 동기화 없이도 스틸링이 안전합니다.

---

### 적응형 백오프

워커는 유휴 이력에 따라 두 스틸 간격 사이를 전환합니다:

| 상태 | 간격 | 동작 |
|------|------|------|
| **활성** (작업을 찾은 직후) | `busyStealIntervalUs` (기본 200μs) | 공격적 스틸링 — 높은 쓰루풋 |
| **유휴** (작업 미발견) | `idleStealIntervalUs` (기본 2000μs) | 보수적 스틸링 — CPU 스핀 감소 |

```cpp
auto interval = idleBackoff_[workerId] ? idleStealIntervalUs_ : busyStealIntervalUs_;
if(semas_[workerId]->try_acquire_for(std::chrono::microseconds(interval))) {
    // ...
}
if(trySteal(workerId, ctx)) {
    idleBackoff_[workerId] = 0;  // found work → busy
    return ctx;
}
idleBackoff_[workerId] = 1;  // no work → idle
```

`idleBackoff_`는 워커 ID로 인덱싱되는 `std::vector<uint8_t>`입니다 — 각 요소는 자체 워커에 의해서만 접근되며 동기화가 필요 없습니다.

---

### 드레인 프로토콜 (우아한 종료)

```
1. beginDrain()
   ├── running_ = false
   ├── draining_ = true
   └── release all semaphores → wake all workers

2. Workers finish current batch
   └── finalize() → retirePendingWork()가 pendingWork_ 감산 (acq_rel)

3. When pendingWork_ reaches 0
   └── Last worker releases all semaphores → all workers break out of loop

4. stop()
   ├── Join all worker threads
   └── Clear pending state
```

---

## 액터 스레드 안전성

### 단일 실행 보장

액터의 `handle()`은 그 액터의 실행 토큰을 보유한 워커 한 곳에서만 호출됩니다. MPSC 메일박스는 소비자가 하나뿐임을 보장하고, 어떤 스레드든 push할 수 있습니다.

```
Thread A: enqueue(msg1) → mailbox push → dispatch() → 슬롯 0→1 성공 → 토큰 발행
Thread B: enqueue(msg2) → mailbox push → dispatch() → 슬롯 이미 1 → dedup, 스킵
Worker 2: pop(토큰) → run() → handle(msg1) → handle(msg2)
```

액터 수준 락이 필요 없습니다 — 메일박스의 단일 소비자 속성과 슬롯의 원자 교환이 상호 배타를 제공합니다.

---

### In-Flight 슬롯 (Deduplication Gate)

**파일:** `src/core/actor_system/runtime/dispatcher/work_dispatcher.hpp` (`InFlightSlot`, `kMaxActors=1024`)
**상세 설계:** [작업 분배](work_dispatch.md) §4~6

액터별 "실행 토큰이 살아있음" 플래그. 캐시라인 정렬된 슬롯 배열에 담기며 **모든 연산이 `exchange`(RMW, acq_rel)**입니다.

**생산자 측 (`dispatch()`):**

```cpp
if(inFlightSlot(actorId).held.exchange(1, std::memory_order_acq_rel)){
    V2_METRICS()->recordDispatch(true, 0);   // dedup — 기존 토큰이 처리 보장
    return true;
}
// 승자만 토큰 발행: 큐 push + pendingWork++ + 세마포어 release
```

**소비자 종료국면 (`finalize()`):** 반납 → 메일박스 재확인 → 재획득/회수

```cpp
releaseInFlight(actorId);                                  // exchange(0, acq_rel)
if(!rt->isStopped() && rt->mailboxCount() != 0 && claimInFlight(actorId)){
    return redispatch(rt);                                 // 토큰 이양 (±0)
}
retirePendingWork();                                       // 토큰 소멸 (−1)
```

plain `store()` 대신 RMW(`exchange`)만 쓰는 것이 핵심입니다. 같은 원자 변수에 대한 RMW들은
수정 순서가 곧 happens-before 체인이 되어, seq_cst fence 없이 lost-wakeup 없는 dedup이 성립합니다.

---

### 재시작 카운터 CAS 루프

**파일:** `src/core/actor_system/runtime/actor_runtime/actor_runtime.cpp:105-113`

```cpp
bool ActorRuntime::tryRestart(const std::string& reason, int maxRestarts) {
    int prev = restartCount_.load(std::memory_order_relaxed);
    while(true) {
        if(prev >= maxRestarts) return false;
        if(restartCount_.compare_exchange_weak(prev, prev + 1,
            std::memory_order_relaxed)) break;
    }
    performRestart(reason);
    return true;
}
```

스핀 루프에서 `compare_exchange_weak`를 사용하여 `maxRestarts` 이하일 때만 재시작 카운트를 원자적으로 증가시킵니다. 이를 통해 여러 슈퍼바이저 콜백 간 TOCTOU 레이스를 방지합니다. `relaxed` 오더링은 CAS 자체가 원자성을 제공하고 다른 데이터가 오더링에 의존하지 않으므로 충분합니다.

---

### 중지 플래그

**파일:** `src/core/actor_system/runtime/actor_runtime/actor_runtime.hpp:70`

```cpp
std::atomic<bool> stopped_{false};
```

`processBatch()` 시작 부분에서만 확인되므로 `memory_order_relaxed`로 설정됩니다. relaxed 오더링은 충분합니다 — 실제 상태 전이(`actor_->close()`)가 자체 오더링을 제공하고, 이 플래그는 조언적(advisory) 성격입니다.

---

## 메모리 오더링 참조

| 위치 | 연산 | 오더링 | 근거 |
|------|------|--------|------|
| MPSC/MPMC `push` | `tail_`에 CAS | `relaxed` | 경쟁 해결만; 데이터 의존성 없음 |
| MPSC/MPMC `push` | `slot.sequence` 쓰기 | `release` | placement-new를 소비자에게 게시 |
| MPSC `pop` | `slot.sequence` 읽기 | `acquire` | 게시된 요소를 읽음 |
| MPSC `pop` | `head_` 쓰기 | `relaxed` | 단일 소비자, 데이터 의존성 없음 |
| MPMC `pop` | `head_`에 CAS | `relaxed` | 소비자 간 경쟁 해결 |
| MPMC `pop` | `slot.sequence` 읽기 | `acquire` | 게시된 요소를 읽음 |
| MPMC `pop` | `slot.sequence` 쓰기 | `release` | 프로듀서를 위해 슬롯 재활용 |
| `Worker::running_` | 쓰기/읽기 | `release`/`relaxed` | 전이 시 쓰기 펜스; 읽기는 조언적 |
| `WorkDispatcher::running_`, `draining_` | 쓰기/읽기 | `release`/`relaxed` | 동일 패턴 |
| `WorkDispatcher::pendingWork_` | `fetch_add` | `relaxed` | 증가는 단방향 |
| `WorkDispatcher::pendingWork_` | `fetch_sub` | `acq_rel` | 워커와 드레인 동기화 — 감산 스레드가 이전 모든 쓰기를 봐야 함 |
| `WorkDispatcher` inFlight 슬롯 | `exchange` (set/claim/release) | `acq_rel` | RMW 체인으로 dedup 정합 보장 — plain store 금지 (lost wakeup 방지) |
| `ActorRuntime::restartCount_` | `compare_exchange_weak` | `relaxed` | 스핀 루프; 원자성만 충분 |
| `ActorRuntime::stopped_` | 쓰기/읽기 | `relaxed` | 조언적 플래그; 실제 상태 전이가 오더링 제공 |
| 메트릭 `updatePeak` | `compare_exchange_weak` | `relaxed` | 최선 노력 피크 추적 |

---

## 이벤트 루프 통합

### 스레드 간 포스팅

**파일:** `src/infra/platform/linux/event_loop_epoll.hpp:38`, `event_loop_epoll.cpp:110-118`

이벤트 루프는 전용 스레드(`v2-main`)에서 실행됩니다. 어떤 스레드든 락프리 MPSC 큐를 통해 작업을 포스팅할 수 있습니다:

```cpp
void EventLoopEpoll::post(std::function<void()> op) {
    while(!pendingOps_.push(std::move(op))) {
        std::this_thread::yield();  // busy-wait if queue full
    }
    // Wake epoll_wait via eventfd
    uint64_t one = 1;
    ::write(stopFd_, &one, sizeof(one));
}

void EventLoopEpoll::drainPendingOps() {
    std::function<void()> op;
    while(pendingOps_.pop(op)) {
        op();
    }
}
```

이벤트 루프는 매 `epoll_wait` 전에 `pendingOps_`를 드레인하여 모든 스레드 간 연산이 신속히 처리되도록 합니다. `eventfd` 쓰기가 차단된 `epoll_wait`를 즉시 깨웁니다.

---

### 스레드 친화성 인식

`subscribe()`와 `unsubscribe()`는 이벤트 루프 스레드에서 호출되었는지 감지합니다:

```cpp
int EventLoopEpoll::subscribe(WatchedFd fd, Handler handler) {
    bool isLoopThread = std::this_thread::get_id() == threadId_;
    {
        std::lock_guard lock(handlersMutex_);
        handlers_[fd] = std::move(handler);
    }
    auto add = [this, fd](){ /* epoll_ctl ADD */ };
    if(isLoopThread) return add();      // direct call
    post([add = std::move(add)]() mutable { add(); });  // deferred
    return Ok;
}
```

루프 스레드에서 호출되면 `epoll_ctl`이 직접 실행됩니다. 그렇지 않으면 해당 연산이 루프 스레드에서 실행되도록 포스팅되어, `epoll_ctl` 호출을 동시에 락할 필요를 없앱니다.

---

## 스레드 로컬 저장소

### TCMalloc 스타일 메모리 캐시

**파일:** `src/core/common/memory/thread_local_cache.hpp`

각 스레드는 풀별 `ThreadLocalCache` 객체 배열을 갖습니다 — 크기 클래스당 1개:

```cpp
inline thread_local std::array<ThreadLocalCache, kMaxPools> poolCaches;
```

**할당 경로 (락 없음):**

```cpp
void* allocate(std::size_t size) {
    auto& cache = caches_[SizeClass::index(size)];
    if(cache.freeList.count() > 0) return cache.freeList.pop();  // fast path
    return fetchFromCentral(idx);  // slow path: batch refill from CentralCache (mutex)
}
```

**해제 경로 (락 없음):**

```cpp
void deallocate(void* ptr, std::size_t size) {
    auto& cache = caches_[SizeClass::index(size)];
    cache.freeList.push(ptr);
    if(cache.freeList.count() > SizeClass::batchSize(idx)) {
        returnToCentral(idx);  // batch return to CentralCache (mutex)
    }
}
```

**3계층 아키텍처:**

```
ThreadLocalCache (lock-free FreeList per size class)
    ↓ cache miss (batch refill)
CentralCache (mutex-guarded, per size-class)
    ↓ out of space
Slab (raw memory)
```

스레드 로컬 계층이 핫 패스의 경쟁을 제거합니다. 뮤텍스 경쟁은 빈번하지 않은 배치 리필 및 반환 시에만 발생합니다.

---

### 워커 전용 백오프 상태

**파일:** `src/core/actor_system/runtime/dispatcher/work_dispatcher.hpp:59`

```cpp
std::vector<uint8_t> idleBackoff_;  // indexed by worker ID
```

각 요소는 자체 워커에 의해서만 읽기/쓰기됩니다 — 동기화 불필요. 스틸 간격 선택을 위해 유휴 백오프 모드로 전환되었는지 추적합니다.

---

## 백프레셔 & 흐름 제어

| 메커니즘 | 동작 |
|----------|------|
| **세마포어 블로킹** | 유휴 시 워커가 `std::counting_semaphore`에서 슬리프; 프로듀서가 `release()`로 깨움 |
| **메일박스 가득 참** | 경고와 함께 메시지 드롭. 의도적 설계 — 전달 보장보다 가용성 우선 |
| **디스패처 큐 가득 참** | `pendingActorList_`(뮤텍스 보호)로 폴백; 유휴 시간에 재시도 |
| **적응형 작업 스틸링** | 활성(200μs) → 유휴(2000μs) 스틸 간격으로 전환하여 CPU 스핀 감소 |
| **메일박스 용량** | 액터별 생성 시 설정 가능; 용량 부족 시 쓰루풋 저하 |

---

## 요약

| 항목 | 설명 |
|------|------|
| **큐 알고리즘** | Vyukov MPSC (메일박스) + Vyukov MPMC (작업 스틸링) |
| **핫 패스 락** | 제로 — 모든 큐잉/디스패치/획득/run 연산이 락프리 |
| **액터 격리** | 단일 소비자 메일박스 + inFlight 슬롯 게이트 = 액터 수준 락 불필요 |
| **작업 분배** | 결정적 친화성 + 부하 인식 디스패치 + 적응형 작업 스틸링 |
| **세마포어 웨이크업** | 워커별 `std::counting_semaphore` + 스핀-던-파크 계층(`parkSpinNs`) — 깨우기 비용 최소화 |
| **메모리 오더링** | 최소화: `relaxed` 가능한 곳에, 데이터 게시에 `acquire`/`release`, 슬롯 게이트는 `acq_rel` exchange만 사용 (`seq_cst` 0회) |
| **스레드 로컬 할당** | TCMalloc 스타일 3계층 할당자로 할당자 경쟁 제거 |
| **거짓 공유 방지** | 핫 패스 원자 모두 `kCacheLine`(64 또는 128바이트)으로 정렬 |
| **백프레셔** | 가득 차면 드롭(메일박스) + 폴백 큐(디스패처) + 적응형 백오프(스틸링) |
