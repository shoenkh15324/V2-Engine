# V2-Engine 로드맵

---

## 일정

```
Phase 1: 성능 병목 제거 ✅ 완료
Phase 2: actor_system 리팩토링 ✅ 완료
Phase 3: 메모리/전송 최적화 ✅ 완료
Phase 4: 아키텍처 고도화 🔄 진행 중
  4-1~4-4: ✅ 완료
  4-5: 🔄 잔여 진단 3건
  4-6: ✅ 문서화
  4-7: 🔄 정확성 하드닝 (리뷰 항목 추가, 1건 완료)
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
| 메시지 시스템 타입 에러제이션 (SBO 64B, `sizeof(Message)` 96B — 초기 문서의 72B 표기는 검증 중 정정) | ✅ |
| 전역 로깅 뮤텍스 제거 (TLS 버퍼, `std::format`, stderr lock-free) | ✅ |
| 메모리 순서 최적화 (`running_` seq_cst → relaxed/release) | ✅ |
| 메트릭 핫 패스 최적화 (지연 count, 비활성화 시 zero-overhead) | ✅ |
| ActorRegistry 락 분할 (`std::mutex` → `std::shared_mutex`) | ✅ |

---

## Phase 4: 아키텍처 고도화 🔄

> **목표**: 정확성 + 타입 안전 + 장애 처리 + 로드 밸런싱 + 문서화

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

### 4-5. 스케일링 성능 붕괴 해소 🔄

> **목표**: 멀티 워커/액터 확장 시 처리량 붕괴의 근본 원인 제거

#### 완료된 작업

| 작업 | 커밋 |
|------|------|
| `inFlight_` 슬롯 전환 (scheduled_ 제거, finalize 프로토콜) | `0ab017c` |
| 근본 원인 확정: futex 수면 진동 (V2_DIAG 실측) | — |
| 스핀-던-파크 Site A+B (parkSpinNs, tokenGraceNs) | `35a1fa8` |
| 멀티 프로듀서 벤치 (`--producers N`) + 50μs 완료 대기 | `0da79ae` |
| 양의 스케일링 입증 (a=1→8: **2.98x speedup**) | — |
| 문서화 (scheduling.md, concurrency.md 갱신) | `02e74a0` |

#### 잔여 진단 (3건)

| 과제 | 상세 | 상태 |
|------|------|------|
| **A≥W 절벽 진단** | a==w 경계에서만 붕괴(96K). V2_DIAG=1 stdout 캡처(`tee /tmp/diag_w16.txt`)로 idleMs vs busyMs 비율 판별 — 라우팅 폭풍 vs 깨우기 비용 중 어떤 것이 원인인지 | ⬜ |
| **Latency P50 회귀** | P50 481ns→1445ns. `bench_latency.cpp`에 `--park-spin-ns`/`--token-grace-ns` 패스스루 미추가. grace=0 대조 실험 필요 | ⬜ |
| **contention/backpressure 변동** | contention −16%, backpressure 드롭율 변동. 위 수정 후 재확인 | ⬜ |

### 4-6. 문서화 ✅

> **목표**: 시스템 전체 아키텍처·실행 모델·설정을 문서화하여, 개발자가 코드 없이도 시스템을 이해할 수 있게 함
>
> **배경**: 런타임 코드(特别是 토큰 생명주기, 디스패치 흐름, 설정 체인)가 복잡해져서, 문서 없이는 수정·디버깅이 어려운 상태
>
> **결과**: 원래 계획한 파일 구성 대신 concepts/layers 2계층으로 재편하여 완료 —
> `token_lifecycle.md` → `concepts/work_dispatch.md`, `data_flow.md` → `concepts/messaging.md` + 아키텍처 README 흐름도,
> API/설정 문서는 `layers/core/*.md`와 각 concepts 문서의 "설정" 절로 흡수.
> 읽기 순서·난이도 가이드는 `concepts/README.md` 제공.

#### 6-1. 핵심 개념 문서

| 문서 | 내용 | 대상 |
|------|------|------|
| `concepts/token_lifecycle.md` | 실행 토큰의 탄생→finalize→소멸. inFlight 슬롯, `exchange(acq_rel)` RMW 체인, lost-wakeup 방지. 토큰 상태 다이어그램 | 런타임 수정자 |
| `concepts/data_flow.md` | 메시지가 발신자→메일박스→dispatch→디스패처 큐→워커→run→handle까지 흐르는 전체 경로. 시퀀스 다이어그램 | 신규 기여자 |
| `concepts/config.md` | 설정 키 전체 목록 (`park_spin_ns`, `token_grace_ns`, `busy_steal_interval_us` 등) + 각 키가 실제로 소비되는 코드 위치 + `enable_pmu` 같은 죽은 키 목록 | 설정 튜닝 시 |

#### 6-2. API 레퍼런스

| 문서 | 내용 | 대상 |
|------|------|------|
| `api/actor_api.md` | `sendMsg`, `sendMsgAfter`, `receiveMsg`, `startTimer`, `cancelTimer` + 코드 예제 | 액터 작성자 |
| `api/runtime_api.md` | `ActorSystem::createActor`, `start`, `stop`, `run` + `ActorSystemConfig` 전체 | 시스템 통합자 |

#### 6-3. 운영·디버깅 문서

| 문서 | 내용 | 대상 |
|------|------|------|
| `ops/troubleshooting.md` | 흔한 실수 (핸들러 누락, 데드락 패턴, 메일박스 포화), V2_DIAG 활용법, 시그널 덤프 해석 | 디버깅 시 |
| `ops/bench_guide.md` | 벤치마크 종류, 실행 방법, `V2_DIAG=1`, 결과 해석 (배치 크기, idle 비율, wake 비용) | 성능 측정 시 |

#### 6-4. 문서 품질 관리

| 작업 | 상세 | 상태 |
|------|------|------|
| `docs/README.md` | 문서 전체 목차 + 읽는 순서 가이드 | ⬜ |
| 용어집 | `glossary.md` — 실행 토큰, inFlight, finalize, dedup, 스틸링 등 용어 정의 | ⬜ |
| 문서-코드 동기화 검증 | `grep`으로 문서 속 함수명/값이 실제 코드에 존재하는지 점검 스크립트 | ⬜ |

### 4-7. 정확성·신뢰성 하드닝 🔄

> **목표**: 극단 시나리오(홍수·소비자 부재·느린 클라이언트)에서도 메시지/스케줄링/수명주기가 결정적

#### 4-7.0 코어 코드 리뷰 발견 항목 (2026-08)

> WorkDispatcher·ActorRuntime 중심의 책임 경계/네이밍/유지보수성 리뷰에서 추출.
> 워크 스틸링·슈퍼바이저·로드 밸런싱 추가로 두 클래스에 기능이 집중되며 생긴 항목들.

| 우선순위 | 작업 | 상세 | 상태 |
|---------|------|------|------|
| P1 | inFlight 슬롯 1024 하드캡 방어 | `actorId % kMaxActors` 모듈러라 1024 초과 시 서로 다른 액터가 슬롯 공유 → dedup 교차(액터 굶음). assert+로그+상한 문서화 또는 동적 확장 필요 | ⬜ |
| P1 | `ActorState::Inherited` 제거 | 선언만 존재하는 dead enum | ⬜ |
| P2 | `ActorRuntime` 생성자 Deps struct화 | 협력자 포인터 5개 + `WorkDispatcher` 생성자 int 7개 positional 전달 — 교환 실수 유발 | ⬜ |
| P2 | `ISupervised::popMessage` 용도 명확화 | 데드 레터 감사용임이 인터페이스에서 안 보임 (`popDeadLetter` 등 개명 검토) | ⬜ |
| P3 | 네이밍 정리 (dispatcher) | `finalize`→`settleToken`, `drainPendedActor`→`drainPendingActors`(멤버 `pendingActorList_`와 철자 통일), `redispatch`는 dedup 스킵 의미 명시 | ⬜ |
| P3 | `Scheduler` 역할 혼동 해소 | 실제론 타이머 메시지 배달기 — 실행 스케줄링과 무관. 리네임은 service/app 파급 커서 별도 결정 | ⬜ |
| P4 | 오더링 근거 코드 주석화 | concurrency.md 오더링 참조 표의 근거를 해당 원자 연산 위치 주석으로 이식 | ⬜ |
| P4 | `Actor(name, id=-1)` 매직 디폴트 제거 | uint64 max 센티널 | ⬜ |

#### 4-7.1 백프레셔 계약

| 작업 | 상세 | 상태 |
|------|------|------|
| dispatch/redispatch 실패 시 좌초 해소 | `enqueue()`/`run()`에서 dispatch 실패 시 재스케줄 경로 보장 | ✅ (`pendingActorList_` 폴백 + `drainPendedActor` idle 재시도) |
| 메일박스 드롭 시 발신자 통지 | NACK/dead-letter 라우팅 옵션 | ⬜ |

#### 4-7.2 Supervision 후속

| 작업 | 상세 | 상태 |
|------|------|------|
| Dead-letter 소비자/관측 | `DeadLetterQueue`에 소비/재시도/로깅 경로 추가 | ⬜ |
| 재시작 지연·백오프 | 영구 실패 액터에 지수 백오프 + 서킷브레이커 | ⬜ |

#### 4-7.3 논블로킹 전송

| 작업 | 상세 | 상태 |
|------|------|------|
| 비차단 소켓 + write 큐 | UDS `::send` 블로킹 제거 → `O_NONBLOCK` + EPOLLOUT | ⬜ |

#### 4-7.4 Timer·MemoryPool 후속

| 작업 | 상세 | 상태 |
|------|------|------|
| 반복 타이머 드리프트 재앵커 | `expiry += interval` → `Clock::now() + interval` | ⬜ |
| MemoryPool `kMaxPools` OOB 가드 | `poolId_` 경계 검증 | ⬜ |
| poison 정책 완성 | allocate fill + magic 기반 이중 해제 감지 | ⬜ |

### 4-8. 서비스/설정 정리 ⬜

> **목표**: 미사용 코드/설정 정리 + 문서-코드 일치

| 작업 | 상세 | 상태 |
|------|------|------|
| `close()` 시 구독자 정리 | 시스템/디바이스 매니저의 `subscribers_` 미정리 해소 | ⬜ |
| wifi 명령 dead-end 해소 | `NmStatusRequest` 미전송, CLI 응답 미도달 | ⬜ |
| 미사용 메시지 정리 | `DbusRegisterResult` 등 미생산 메시지 제거 | ⬜ |
| PMU 백엔드 런타임 선택 | `__aarch64__` 컴파일타임 고정 → 보드 탐지/fallback | ⬜ |
| `enable_pmu` 죽은 키 처리 | 설정에만 존재, 런타임 미파싱 | ⬜ |
| 설정 검증 | `loadFromFile`의 `catch(...)` → 스키마 검증/미지 키 경고 | ⬜ |
| 액터 이름 상수화 | 하드코딩 문자열 10+ 사이트 → 상수/핸들 | ⬜ |

---

## Phase 5: 벤치마크 & 배포 ⬜

> **목표**: 벤치마크 수치 갱신 + 테스트 커버리지 확대 + 프로덕션 배포 준비

### 벤치마크 갱신

| 작업 | 상세 | 상태 |
|------|------|------|
| 6개 벤치 재실행 (스핀 티어 적용 후) | throughput/latency/contention/scaling/backpressure/scheduler — 워커 1→64 스위프 | ⬜ |
| 수치·결론 갱신 | `docs/benchmark/*.md`의 과거 수치 교체, 결론 재작성 | ⬜ |
| 멀티 프로듀서 모드 공식화 | throughput에 `--producers N` 기본 옵션 승격 | ⬜ |

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
| non-root 실행 | systemd `User=`/샌드박스 | ⬜ |
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
