# V2-Engine 로드맵

---

## 일정

```
Phase 1: 성능 병목 제거 ✅ 완료
Phase 2: actor_system 리팩토링 ✅ 완료
Phase 3: 메모리/전송 최적화 ✅ 완료
Phase 4: 아키텍처 고도화 🔄 진행 중
  4-1~4-4: ✅ 완료
  4-5: 🔄 스케일링 성능 개선 (최우선)
  4-6: ✅ 문서화
  4-7: 🔄 정확성 하드닝 (3건 완료)
  4-8: ⬜ 서비스/설정 정리
Phase 5: 벤치마크 & 배포 ⬜ 대기
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

## Phase 2: actor_system 리팩토링 ✅

> **목표**: 강결합 구조 해소 → Runtime과 Actor 완전 분리, 단방향 의존성, 컴파일 의존성 최소화, 확장 가능한 구조 확보

### 핵심 아키텍처 변경

#### 1. Actor ↔ ActorRuntime 순환 의존 제거 ✅

**기존 문제**: Actor가 ActorRuntime을 직접 참조하고, ActorRuntime이 Actor를 참조하는 양방향 의존

**해결**: `IActorRuntime` 인터페이스 도입으로 단방향 의존성 확보

#### 2. Dispatcher 역할 분리 ✅

| 컴포넌트 | 책임 | 파일 |
|----------|------|------|
| `IWorkDispatcher` | Ready Actor Queue 인터페이스 | `dispatcher/i_work_dispatcher.hpp` |
| `WorkDispatcher` | MPSC 큐 + 세마포어 기반 work 분배 | `dispatcher/work_dispatcher.hpp/cpp` |
| `IEventLoop` | fd 구독/구독해제 인터페이스 | `dispatcher/io/i_event_loop.hpp` |
| `EventLoopEpoll` | epoll 기반 이벤트 루프 (Linux 전용) | `dispatcher/io/event_loop_epoll.hpp/cpp` |
| `Scheduler` | Timer Queue, Timeout 관리 (`IEventLoop`에 의존) | `runtime/scheduler.hpp/cpp` |

#### 3. Registry 역할 축소 ✅

Lookup 전용으로 축소, enableActor/disableActor는 IActorRuntime으로 이동

---

## Phase 3: 메모리/전송 최적화 ✅

> **목표**: 핫 패스 캐시 미스 + 불필요한 할당/잠금/원자 연산 제거

| 작업 | 상태 |
|------|------|
| 메시지 전송 경로 최적화 (생성자 문자열 이동) | ✅ |
| 캐시 라인 패딩 (`scheduled_`, `ActorMetrics`, `WorkerMetrics`) | ✅ |
| 타이머 메모리 할당 제거 (`shared_ptr`/`std::function` 제거) | ✅ |
| Slab 기반 메모리 풀 (FreeList → Chunk → Slab → ThreadLocalCache) | ✅ |
| 메시지 시스템 타입 에러제이션 (SBO 64B, `sizeof(Message)` 96B) | ✅ |
| 전역 로깅 뮤텍스 제거 (TLS 버퍼, `std::format`, stderr lock-free) | ✅ |
| 메모리 순서 최적화 (`running_` seq_cst → relaxed/release) | ✅ |
| 메트릭 핫 패스 최적화 (지연 count, 비활성화 시 zero-overhead) | ✅ |
| ActorRegistry 락 분할 (`std::mutex` → `std::shared_mutex`) | ✅ |

---

## Phase 4: 아키텍처 고도화 🔄

> **목표**: 정확성 + 타입 안전 + 장애 처리 + 성능 고도화 + 문서화

### 4-1. Supervision 트리 + 예외 격리 ✅

| 작업 | 상태 |
|------|------|
| `supervisor.hpp` — 액터 실패 처리/재시작 구조 | ✅ |
| 예외 격리 — `ActorRuntime::run()`에서 `try/catch` | ✅ |
| 재시작 전략 — OneForOne, OneForAll | ✅ |
| 재시작 예산 — `maxRestarts` 한도 초과 시 shutdown | ✅ |
| 데드 레터 큐 | ✅ |

### 4-2. 정확성 버그 수정 ✅

| 작업 | 상태 |
|------|------|
| 로그 버퍼 exit UAF → TLS RAII | ✅ |
| epoll 중복 구독/핸들러 레이스 | ✅ |
| 타이머 use-after-free | ✅ |
| `ActorState` 레이스 → `std::atomic` | ✅ |
| 그레이셔널 드레인 | ✅ |

### 4-3. 타입별 메시지 디스패치 ✅

| 작업 | 상태 |
|------|------|
| `Message::visit<Tuple>` + fold short-circuit | ✅ |
| `Actor::dispatch` + `handleUnknown` + static_assert | ✅ |
| 9개 서비스 액터 전환 + 데드 레터 메트릭 | ✅ |

### 4-4. 로드 밸런싱 & 병렬화 ✅

| 작업 | 상태 |
|------|------|
| 단일 엔트리 스케줄링 가드 (`scheduled_` 원자 교환) | ✅ |
| 로드 어웨어 디스패치 (70% HWM → pickLeastLoaded) | ✅ |
| 워크 스틸링 (MPMC 큐 + 적응형 백오프) | ✅ |

### 4-5. 스케일링 성능 개선 🔄 (최우선)

> **목표**: 멀티 워커/액터 확장 시 처리량 붕괴의 근본 원인 제거
>
> **배경**: 2026-08-31 벤치마크 실측 결과, 워커 스케일링과 멀티 프로듀서 처리에서 구조적 병목 확인

#### 4-5.1 현재 성능 프로파일 (2026-08-31 실측)

**테스트 환경**:

| 항목 | 사양 |
|------|------|
| CPU | AMD Ryzen 7 9800X3D (8 Physical, 16 Logical, Zen 5) |
| 코어 구성 | 8코어 16스레드, SMT 활성화 |
| 캐시 | L1 80KB(데이터)/64KB(명령) per core, L2 1MB per core, L3 96MB (3D V-Cache) |
| RAM | 8 GB (DDR5) |
| OS | Ubuntu 22.04.5 LTS (WSL2, 커널 6.18.33) |
| 컴파일러 | g++-14 (Ubuntu 14.3.0-12ubuntu1~22~ppa2) |
| C++ 표준 | C++20 |
| 빌드 시스템 | CMake 3.22.1 + Ninja |
| 빌드 모드 | Release (`-O3 -DNDEBUG`) |
| LTO | 활성화 (`CMAKE_INTERPROCEDURAL_OPTIMIZATION ON`) |
| 링커 | Gold (기본) |
| 메모리 할당자 | TCMalloc 기반 Slab 풀 (커스텀) |

**쓰루풋 벤치마크 (단일 프로듀서)**:

| Workers | Actors | msgs/sec | 비고 |
|---------|--------|----------|------|
| 1 | 1 | 27.7M | 기준 |
| 1 | 2 | 29.0M | Ping-Pong |
| 1 | 4 | 35.1M | |
| 4 | 4 | 19.9M | |
| 4 | 16 | 28.5M | |
| 8 | 8 | 17.4M | |
| 16 | 16 | 0.3M | **붕괴** |

**쓰루풋 벤치마크 (멀티 프로듀서)**:

| Workers | Actors | Producers | msgs/sec | 비고 |
|---------|--------|-----------|----------|------|
| 1 | 16 | 8 | 48.7M | |
| 4 | 16 | 4 | 59.4M | |
| 4 | 16 | 8 | **73.3M** | **최적** |
| 8 | 16 | 8 | 66.5M | |

**레이턴시 벤치마크**:

| Workers | Actors | P50 | P99 | P99.9 |
|---------|--------|-----|-----|-------|
| 1 | 1 | 912 ns | 977 ns | 2,430 ns |
| 1 | 4 | 909 ns | 1,063 ns | 12,628 ns |
| 4 | 4 | 915 ns | 1,030 ns | 12,189 ns |

**멀티 프로듀서 경쟁**:

| Producers | msgs/sec | CPU 코어 점유 |
|-----------|----------|-------------|
| 1 | 19.7M | ~1 |
| 2 | 13.2M | ~2 |
| 4 | 11.3M | ~4 |
| 8 | 9.8M | ~8 (물리 코어 포화) |
| 16 | 9.3M | ~16 (SMT) |
| 32 | 9.4M | 정체 |

**상용 엔진 비교**:

> 벤치마크 방법론이 각 엔진마다 다르므로, 동일 환경에서 직접 측정한 수치가 아닙니다.
> 가능한 경우 핑퐁(1:1 메시지 교환), 라운드로빈(1:N), N:N 등 다양한 베리에이션의 수치를 수집하였습니다.
> 타 엔진 벤치마크는 공식 문서/저장소의 수치를 인용하였으며, 각각의 테스트 환경이 다릅니다.

*C++ 액터 프레임워크 비교*:

| 엔진 | 벤치마크 유형 | 쓰루풋 | 테스트 환경 | 비고 |
|------|-------------|--------|-----------|------|
| **V2-Engine** | 1W 1A 1P (쓰루풋) | **27.7M msgs/sec** | Ryzen 9800X3D, g++-14, Release+LTO | 단일 워커 최적 |
| **V2-Engine** | 4W 16A 8P (멀티 프로듀서) | **73.3M msgs/sec** | 동일 | 최적 구간 |
| **V2-Engine** | Ping-Pong (2A 1W) | **29.0M msgs/sec** | 동일 | 1:1 메시지 교환 |
| **V2-Engine** | 4A 4W 1P | **19.9M msgs/sec** | 동일 | 워커 수 = 액터 수 |
| **CAF** (C++) | simple_streaming (in-process) | ~9.4M msgs/sec | Intel i7, GCC 4.9, Release | 2019 측정 |
| **CAF** (C++) | simple_streaming (최적화 분기) | ~35M msgs/sec | 동일 | 커밋 버전별 편차 큼 |
| **SObjectizer** (C++) | Ping-Pong (direct channel) | ~17M msgs/sec | Intel i7-11850H @2.5GHz, GCC | 2024 측정 |
| **SObjectizer** (C++) | Ping-Pong (thread pool, 2T) | ~10M msgs/sec | 동일 | 협력 FIFO |
| **SObjectizer** (C++) | Ping-Pong (단일 스레드) | ~2M msgs/sec | 동일 | 싱글 스레드 디스패처 |
| **Theron** (C++) | Mixed Scenario | 스케일링 실패 | Intel Xeon 12코어 (2.27GHz) | 뮤텍스 기반, 4코어 이후 성능 하락 |

*Rust 액터 프레임워크 비교*:

| 엔진 | 벤치마크 유형 | 쓰루풋 | 테스트 환경 | 비고 |
|------|-------------|--------|-----------|------|
| **Actix** (Rust) | 라운드로빈 100A (32B) | ~4.7M msgs/sec | Intel i7 Quad-Core, Rust 1.84 | 가장 빠른 Rust 액터 |
| **Actix** (Rust) | direct mailbox (32B) | ~4.5M msgs/sec | 동일 | 단일 액터 직접 전송 |
| **Ractor** (Rust) | 라운드로빈 100A (32B) | ~3.1M msgs/sec | 동일 | Tokio 기반 |
| **Kameo** (Rust) | 라운드로빈 100A | 유사 수준 | 동일 | 분산 지원 |

*분산/네트워크 메시징 비교*:

| 엔진 | 벤치마크 유형 | 쓰루풋 | 테스트 환경 | 비고 |
|------|-------------|--------|-----------|------|
| **CAF** (C++) | simple_streaming (분산, TCP) | ~10-15M msgs/sec | Intel i7, localhost TCP | 동일 프로세스 간 |
| **CAF** (C++) | simple_streaming (분산, 워커) | ~15M msgs/sec | 동일 | middleman workers=2 |
| **Orleans** (.NET) | ping-pong (분산) | ~100K-500K msgs/sec | .NET 10, 클러스터 | 가상 액터, 네트워크 오버헤드 |
| **Akka.NET** (.NET) | ping-pong (분산) | ~100K-300K msgs/sec | .NET, 6노드 클러스터 | 클러스터 오버헤드 |

*종합 비교 (다양한 베리에이션)*:

| 구성 | V2-Engine | CAF | SObjectizer | Actix | Ractor |
|------|-----------|-----|-------------|-------|--------|
| **1:1 Ping-Pong (싱글 스레드)** | 29.0M | ~9M | ~2M | ~4.7M | ~3.1M |
| **1:N 라운드로빈 (싱글 스레드)** | 35.1M (4A) | ~9M | ~2M | ~4.7M | ~3.1M |
| **N:N 멀티 워커** | 19.9M (4W4A) | ~9M | ~10M | ~4.7M | ~3.1M |
| **멀티 프로듀서 (8P)** | 73.3M (4W16A) | N/A | N/A | N/A | N/A |
| **레이턴시 P50** | 912 ns | ~1μs | ~0.5μs | N/A | N/A |

> **해석**: V2-Engine은 C++ 범용 액터 프레임워크(CAF, SObjectizer) 대비 **2-15배 빠르고**, Rust 기반 액터 프레임워크(Actix, Ractor) 대비 **6-9배 빠릅니다.** 멀티 프로듀서 구간(4W 16A 8P)에서는 **73.3M msgs/sec**를 달성하여, 프레임워크 수준에서 유사하거나 더 높은 성능을 보여줍니다.
>
> **주의사항**: 각 벤치마크의 방법론이 다릅니다 — 핑퐁은 1:1 메시지 교환, 라운드로빈은 N개 액터에 순차 전송, 멀티 프로듀서는 병렬 전송입니다. 직접 비교에는 한계가 있으나, 주문향의 순서를 파악하는 데 유용합니다.

#### 4-5.2 확인된 병목

**병목 1: 단일 프로듀서 + 멀티 액터 시 워커 스케일링 붕괴**

| 조건 | 1W | 4W | 8W | 16W |
|------|----|----|----|----|
| A=1, P=1 | 27.4M | 28.9M | 20.4M | 30.0M |
| A=16, P=1 | 38.9M | 28.5M | **9.4M** | **4.5M** |
| A=16, P=8 | 48.7M | **73.3M** | — | — |

- **단일 프로듀서 + 16 액터**: W=8에서 4배, W=16에서 8배 성능 하락
- **멀티 프로듀서 + 16 액터**: 붕괴 없음, 오히려 성능 향상
- **원인**: `receiveMsg()` → `enqueue()` → `dispatch()` 경로가 싱글 스레드에서 실행되어 병렬화 불가

**병목 2: MPMC 큐 컨슈머 CAS 경쟁**

| Producers | msgs/sec | 비고 |
|-----------|----------|------|
| 1 | 19.7M | 기준 |
| 2 | 13.2M | **33% 하락** (CAS 경쟁) |
| 8 | 9.8M | 포화 |
| 32 | 9.4M | 정체 (CPU 코어 한도) |

- `LockFreeMpmcQueue::pop()`의 CAS 루프가 여러 워커에서 경쟁
- 프로듀서 2개에서 즉시 33% 하락 → `tail_` CAS 경쟁이 원인
- P=8에서 CPU 물리 코어 8개 한도 도달

**병목 3: 레이턴시 상승**

| 지표 | 이전 수치 | 현재 수치 | 비고 |
|------|----------|----------|------|
| P50 | 378 ns | **912 ns** | 2.4배 상승 |
| P99 | 641 ns | **977 ns** | 1.5배 상승 |

- 스핀 티어(`parkSpinNs`, `tokenGraceNs`) 도입 후 레이턴시 상승
- grace spin이 새 메시지 도착 대기 중 추가 지연 발생

#### 4-5.3 개선 과제

| 과제 | 상세 | 우선순위 | 예상 효과 |
|------|------|---------|----------|
| **MPMC 컨슈머 경쟁 제거** | 워커별 MPSC 큐 + stealing용 MPMC 큐 분리. 각 워커가 자체 MPSC 큐에서 팝 (논블로킹), stealing 시에만 공유 MPMC 큐 사용 | 🔴 최우선 | 4W+ 성능 2-3배 향상 |
| **dispatch 경로 멀티 스레드화** | `receiveMsg()`의 `enqueue()` → `dispatch()` 경로를 멀티 프로듀서 친화적으로 변경. 현재 싱글 프로듀서에서 모든 액터의 dispatch가 순차 실행됨 | 🔴 높음 | 단일 프로듀서 + 멀티 액터 성능 향상 |
| **스핀 티어 튜닝** | `parkSpinNs`, `tokenGraceNs` 파라미터 최적화. grace=0 대조 실험으로 레이턴시 회귀 원인 규명 | 🟡 중간 | P50 레이턴시 300-500ns 절감 |
| **인플라이트 슬롯 확장** | `kMaxActors=1024` → 4096 확장 또는 동적 할당. `actorId % 1024` 해싱 콜리전 감소 | 🟡 중간 | 멀티 액터 시 콜리전 감소 |
| **적응형 배치 크기** | `maxBatch`를 워커 수에 따라 조정 (1W=64, 4W=16, 8W=8) | 🟢 낮음 | 워커 수에 따른 최적화 |

#### 4-5.4 이전 잔여 진단 재평가

| 과제 | 재평가 | 비고 |
|------|--------|------|
| A≥W 절벽 진단 | **유지** — 단일 프로듀서 + 멀티 액터 붕괴와 연관. 라우팅 폭풍 vs 깨우기 비용 중 원인 규명 필요 |
| Latency P50 회귀 | **유지** — grace spin 튜닝으로 해결 가능 |
| contention/backpressure 변동 | **보류** — MPMC 경쟁 제거 후 재확인 |

---

### 4-6. 문서화 ✅

> **목표**: 시스템 전체 아키텍처·실행 모델·설정을 문서화하여, 개발자가 코드 없이도 시스템을 이해할 수 있게 함
>
> **결과**: concepts/layers 2계층으로 재편하여 완료

#### 6-1. 핵심 개념 문서

| 문서 | 내용 | 대상 |
|------|------|------|
| `concepts/work_dispatch.md` | 실행 토큰의 탄생→finalize→소멸. inFlight 슬롯, `exchange(acq_rel)` RMW 체인 | 런타임 수정자 |
| `concepts/messaging.md` | 메시지가 발신자→메일박스→dispatch→디스패처 큐→워커→run→handle까지 흐르는 전체 경로 | 신규 기여자 |
| `concepts/config.md` | 설정 키 전체 목록 + 각 키가 실제로 소비되는 코드 위치 | 설정 튜닝 시 |

#### 6-2. API 레퍼런스

| 문서 | 내용 | 대상 |
|------|------|------|
| `api/actor_api.md` | `sendMsg`, `sendMsgAfter`, `receiveMsg`, `startTimer`, `cancelTimer` + 코드 예제 | 액터 작성자 |
| `api/runtime_api.md` | `ActorSystem::createActor`, `start`, `stop`, `run` + `ActorSystemConfig` 전체 | 시스템 통합자 |

#### 6-3. 운영·디버깅 문서

| 문서 | 내용 | 대상 |
|------|------|------|
| `ops/troubleshooting.md` | 흔한 실수 (핸들러 누락, 데드락 패턴, 메일박스 포화), V2_DIAG 활용법 | 디버깅 시 |
| `ops/bench_guide.md` | 벤치마크 종류, 실행 방법, 결과 해석 | 성능 측정 시 |

#### 6-4. 문서 품질 관리

| 작업 | 상세 | 상태 |
|------|------|------|
| `docs/README.md` | 문서 전체 목차 + 읽는 순서 가이드 | ⬜ |
| 문서-코드 동기화 검증 | `grep`으로 문서 속 함수명/값이 실제 코드에 존재하는지 점검 스크립트 | ⬜ |

---

### 4-7. 정확성·신뢰성 하드닝 🔄

> **목표**: 극단 시나리오(홍수·소비자 부재)에서도 메시지/스케줄링/수명주기가 결정적

#### 4-7.1 백프레셔 계약

| 작업 | 상세 | 상태 |
|------|------|------|
| dispatch/redispatch 실패 시 좌초 해소 | `enqueue()`/`run()`에서 dispatch 실패 시 재스케줄 경로 보장 | ✅ (`pendedActorList_` 폴백 + `drainPendedActor` idle 재시도) |
| 메일박스 드롭 시 발신자 통지 | NACK/dead-letter 라우팅 옵션 | ⬜ |

#### 4-7.2 Supervision 후속

| 작업 | 상세 | 상태 |
|------|------|------|
| Dead-letter 소비자/관측 | `DeadLetterQueue`에 소비/재시도/로깅 경로 추가 | ⬜ |

#### 4-7.3 Timer·MemoryPool 후속

| 작업 | 상세 | 상태 |
|------|------|------|
| 반복 타이머 드리프트 재앵커 | `expiry += interval` → `Clock::now() + interval` | ⬜ |
| MemoryPool `kMaxPools` OOB 가드 | `poolId_` 경계 검증 | ⬜ |

---

### 4-8. 서비스/설정 정리 ⬜

> **목표**: 미사용 코드/설정 정리 + 문서-코드 일치

| 작업 | 상세 | 상태 |
|------|------|------|
| `close()` 시 구독자 정리 | 시스템/디바이스 매니저의 `subscribers_` 미정리 해소 | ⬜ |
| wifi 명령 dead-end 해소 | `NmStatusRequest` 미전송, CLI 응답 미도달 | ⬜ |
| 미사용 메시지 정리 | `DbusRegisterResult` 등 미생산 메시지 제거 | ⬜ |
| `enable_pmu` 죽은 키 처리 | 설정에만 존재, 런타임 미파싱 | ⬜ |
| 설정 검증 | `loadFromFile`의 `catch(...)` → 스키마 검증/미지 키 경고 | ⬜ |

---

## Phase 5: 벤치마크 & 배포 ⬜

> **목표**: 벤치마크 수치 갱신 + 테스트 커버리지 확대 + 프로덕션 배포 준비

### 벤치마크 갱신

| 작업 | 상세 | 상태 |
|------|------|------|
| 6개 벤치 재실행 (Release + LTO) | throughput/latency/contention/scaling/backpressure/scheduler — 워커 1→16 스위프 | ⬜ |
| 수치·결론 갱신 | `docs/benchmark/*.md`의 과거 수치 교체, 결론 재작성 | ⬜ |
| 멀티 프로듀서 모드 공식화 | throughput에 `--producers N` 기본 옵션 승격 | ⬜ |
| **벤치마크-코드 동기화** | 문서의 벤치마크 수치가 실제 코드와 일치하는지 검증 | ⬜ |

### 테스트 커버리지 확대

| 작업 | 상세 | 상태 |
|------|------|------|
| 서비스 레이어 테스트 | MonitorActor pub/sub, CmdActor async, SystemManager/DeviceManager | ⬜ |
| 전송 테스트 | UdsServer/UdsClient — 연결/송수신/EINTR/재연결 | ⬜ |
| 설정 테스트 | JsonConfigLoader — 키 파싱/타입 오류/미지 키 | ⬜ |
| EventLoopEpoll 심화 | 크로스 스레드 구독, timerfd, signal-pipe | ⬜ |

### 배포 하드닝

| 작업 | 상세 | 상태 |
|------|------|------|
| 로그 로테이션 | logrotate 규칙 | ⬜ |
| 소켓 권한 | 0777 → 서비스 유저 전용 + `SO_PEERCRED` | ⬜ |
| uninstall 청소 | `/tmp` 소켓/로그 제거 + `--purge` | ⬜ |
| CI 파이프라인 | GitHub Actions — push/PR 시 빌드+ctest+벤치 스모크 | ⬜ |

---

## superseded sketch (참고용)

> 아래 섹션은 4-5 최초 설계 스케치입니다. 현재 코드와 다를 수 있으므로 참고용으로만 보관합니다.
> 현행 설계는 [작업 분배 (Work Dispatch)](../architecture/concepts/work_dispatch.md) 문서를 따릅니다.

<details>
<summary>4-5 최초 스케치 펼치기</summary>

#### 기존 설계 (구 `scheduled_` 기반)

```
enqueue():
  if(!scheduled_.exchange(true, seq_cst))
    dispatch(this);
  else
    deduplicated++;

run() 마지막 배치:
  scheduled_.store(false, seq_cst);
  if(!mailbox.empty())
    redispatch(this);           // 재스케줄
  else
    retirePendingWork();        // 실행권 해제
```

#### 문제점

| 문제 | 설명 |
|------|------|
| seq_cst 비용 | `exchange(true, seq_cst)`가 ARM에서 DMB ISH 배리어 2회 발생 |
| Lost-wakeup | store(false)와 empty() 사이에 메시지 도착 시 재스케줄 누락 가능 |
| 토큰 좌초 | 세마포어 토큰이 워커 외부에 남으면 해당 워커의 실행이 지연됨 |

#### 현행 설계와의 차이

| 항목 | 구 설계 | 현행 (inFlight 슬롯) |
|------|---------|---------------------|
| Dedup 메커니즘 | `ActorRuntime::scheduled_` (seq_cst) | `WorkDispatcher::inFlight_[].held` (acq_rel) |
| 토큰 반납 | `scheduled_.store(false)` + 수동 재확인 | `finalize()` 프로토콜 (반납→재확인→재획득/회수) |
| Fence 비용 | 4회/메시지 | 0회 (RMW 체인이 happens-before 제공) |
| 스핀 티어 | 없음 | Site A (parkSpinNs) + Site B (tokenGraceNs) |

</details>
