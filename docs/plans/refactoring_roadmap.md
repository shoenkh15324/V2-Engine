# V2-Engine Core Layer 리팩토링 로드맵

> **목표**: Core Layer와 Service Layer를 Clean Architecture 원칙에 부합하는 순수 계층으로 재구축 (포트 소유권 정리 포함)
> **기간**: 약 7-10주 (Phase 1-4)
> **기준선**: 시작 20/70 → **현재 ≈ 45/70** → 목표 60+/70
>
> **문서 원칙**: 이 문서는 현재 코드베이스의 상태만 반영한다. 완료 항목은 요약으로, 미완료 항목은 구체적인 작업 목록으로만 기술한다.

---

## 현재 진행 상태 (2026-08-09)

**완료 (Phase 1 대부분 + Phase 2 인프라 이관)**
- core를 독립 CMake subproject로 분리, std(+libc)만 사용 (`v2_core_smoke`가 단독 실행 증명)
- 인터페이스 주입 전환: `ITimer`/`IMemoryAllocator`/`IMailbox`/`IEventLoop`/`IWorkDispatcher`/`IScheduler` 등
- ActorSystem 생성자 주입 + Composition Root(app)로 기본 조립 이관
- ActorRuntime 분해(`processBatch` + 계측 래퍼), 전역 싱글톤 제거(Metrics/Log/MemoryPool)
- 서비스 전용 메시지 전부 `core/messages/` → 소유 서비스 폴더로 이관
- OS 의존 구현체 이관: epoll, signal_handler, LinuxTimer(timerfd) → `infra/platform/linux/`
- 테스트 **132/132 통과**

**남은 작업 (실제 진행할 것)**
- Phase 1.6: HAL 포트 3개 `service/ports/` 이동 + `monitor_data`의 nlohmann 제거
- **Phase 2.5: 데이터 소유자 액터 + 구독(발행/구독) 정책** (2026-08-09 설계 확정 — 아래 섹션)
- Phase 3.1~3.3: Scheduler O(N) 정리(벤치 확인 후), Registry 락 분할, 전역 `thread_local` 정리
- Phase 4.1: `runtime_config.h` 필드 일반화(`epoll*` → `eventLoop*`)

---

## Phase 0: 준비 및 인프라 (완료)

### 0.1 브랜치/CI/메트릭 ✅
- `refactor/core-architecture` 브랜치, 클린 빌드 + `ctest`(132/132) 검증
- `compile_commands.json` 생성 설정, 베이스라인 메트릭 수집

### 0.2 아키텍처 원칙

> **의존 방향 (화살표는 안쪽으로만)**: `v2_app → v2_infra/v2_service → v2_core`. 각 레이어는 자신보다 안쪽 레이어만 링크하고, `v2_core`는 아무것도 링크하지 않는다.
>
> - **core = C++20, std(+libc)만 사용**, 외부 라이브러리 링크 금지, 단독 빌드/실행 가능. OS 의존은 infra가 전담
> - **포트 소유권 = 소비자 소유 (DIP)**: 인터페이스는 사용하는 계층이 소유. `IPmu`/`ISys`/`II2c`는 `service/ports/`로, 구현체는 infra 유지
> - **Port/메시지는 각 도메인 폴더에 co-location**: 별도 `common/interfaces/` 없음. 서비스 전용 메시지는 소유 서비스 폴더, core `messages/`는 엔진 범용 타입만
> - **신규 인터페이스는 실수요가 있을 때만 추가 (YAGNI)**: 추가된 것 — `ITimer`, `IMemoryAllocator`, `IMailbox`. 폐기/미적용 — `ITimeSource`, `ILogger`, `IMetrics` (구현체 1개뿐)
> - **Composition Root는 app**: 구체 타입 생성/주입은 app에서만. service의 플랫폼 분기(`#if V2_PLATFORM_*`) 금지 → 생성자 주입으로 위임
> - **service는 core + 자체 포트만 의존**: infra/3rd-party 직접 참조 금지. **예외: `service/dbus`는 sdbus에 합법적으로 의존**
> - **서비스 간 결합은 메시지/레지스트리 조회로**: 구체 액터 헤더 include 금지
> - **CMake 타깃 의존성으로 경계 강제**: 계층 위반을 링크 단계에서 검출. 단, service 소유 포트를 구현하는 infra는 해당 포트 헤더를 include (header-only, 링크 아님)

**목표 디렉토리 구조**:
```
src/core/                              # C++20, std-only, 단독 빌드/실행
├── actor_system/
│   ├── actor/                         # Actor, ActorHandle, IActorRegistry/ActorRegistry
│   ├── messages/                      # message.hpp, message_traits.hpp, system_messages.hpp
│   ├── runtime/
│   │   ├── actor_runtime/             # ActorRuntime (IMailbox 주입)
│   │   ├── dispatcher/                # Worker, IWorkDispatcher, io/IEventLoop
│   │   ├── scheduler/                 # Scheduler (ITimer 주입)
│   │   ├── supervisor/                # Supervisor, IDeadLetterQueue
│   │   └── mailbox/                   # IMailbox + Mailbox 어댑터
│   └── actor_system.hpp/cpp           # 엔진 조립 API (구체 타입 없음)
├── common/
│   ├── config/                        # platform_config.h, runtime_config.h (구조체만)
│   ├── container/                     # cache_line, lock_free_mpsc_queue, ring_buffer
│   ├── time/                          # time.hpp/cpp, sleep.hpp
│   ├── timer/                         # i_timer.hpp, timer_base.hpp/cpp, timer.hpp/cpp (portable)
│   ├── memory/                        # i_memory_allocator.hpp, memory_pool, slab, thread_local_cache
│   ├── log/                           # log.hpp/cpp (Logger — activeLogger)
│   └── util/                          # return.hpp, debug.hpp
└── perf/metrics/                      # metrics.hpp (activeMetrics)

src/infra/                             # OS/외부 라이브러리 의존만
├── platform/linux/                    # event_loop_epoll, timer_linux, signal_handler, epoll
├── config/json_config_loader.hpp/cpp  # nlohmann_json 의존
├── transport/uds/uds_server.hpp/cpp   # IPC 서버 구현
├── hal/{pmu,sys,i2c}/                 # IPmu/ISys/II2c 구현체 (포트는 service/ports/)
├── memory/                            # 특수 할당자 (memory_pool_allocator)
├── ui/ftxui_renderer.hpp/cpp          # ftxui 의존
└── mock/                              # MockAllocator, TestRegistry 등

src/service/                           # 비즈니스 로직 (core + 자체 포트만 의존)
├── {device_manager,network_manager,monitor,dbus,ipc,cmd,system,tick}/
│   ├── *_actor.hpp/cpp
│   └── *_messages.hpp
└── ports/                             # i_pmu.hpp, i_sys.hpp, i_i2c.hpp (소비자 소유 Port)

bench/                                 # 벤치마크 (독립 타깃 v2_bench)
app/                                   # CLI/TUI/main — Composition Root
```

### 0.3 의존성 주입 컨테이너 ✅
- `core/common/di/service_container.hpp` — `ServiceContainer`/`ServiceProvider`, 생성자 주입만 지원, 컴파일 타임 바인딩 우선

---

## Phase 1: 아키텍처 경계 복원 — **P0 Critical (완료 + 잔여 2건)**

### 1.1 Core CMake 정리 + 독립 subproject 분리 ✅
- `src/core/CMakeLists.txt` 독립 project, `v2_core`는 `Threads::Threads`만 링크. core→infra 링크 제거, compile definitions는 `app.cmake`로 이관
- `test/standalone/main.cpp` (`v2_core_smoke`) — v2_core만 링크해 외부 심볼 무참조 증명, core portable `Timer` + mock event loop로 메시징/타이머 실제 실행
- nlohmann 파싱 → `infra/config/json_config_loader.cpp` 이관, `runtime_config.cpp` 삭제
- 타이머: portable `Timer`(스레드+세마포어) core `common/timer` 유지, infra `LinuxTimer`(timerfd)가 Linux에서 대체. 선택은 Composition Root
- **검증**: `rg 'infra/|nlohmann/|ftxui/' src/core` → 0, standalone 빌드 성공, 전체 `ctest` 132/132 통과

### 1.2 핵심 인터페이스 정의 ✅
- 추가: `core/common/timer/i_timer.hpp`(ITimer), `core/common/memory/i_memory_allocator.hpp`(IMemoryAllocator), `core/actor_system/runtime/mailbox/i_mailbox.hpp`(IMailbox)
- 폐기: `i_time_source.hpp`(참조처 없음 — `Time`으로 충분), `i_logger.hpp`/`i_metrics.hpp`(구현체 1개뿐 — `activeLogger()`/`activeMetrics()` 패턴)
- 기존 Port 유지: `i_actor_registry.hpp`, `i_supervisor.hpp`, `i_work_dispatcher.hpp`, `i_event_loop.hpp`, `i_scheduler.hpp`(타이머 위임 포함)

### 1.3 ActorSystem 생성자 주입 리팩토링 ✅
- 공개 계약에서 구체 타입 제거: `ActorSystemConfig` + `ActorSystemDeps`(전부 `unique_ptr<인터페이스>`)
- `createDefaultActorSystem(config, eventLoop, timer)` 팩토리로 기본 조립 분리 (구체 생성은 app에서)
- `IEventLoop`/`ITimer` null 가드 — 루프 없이 생성 가능
- app/bench 호출부를 `createDefaultActorSystem`으로 치환

### 1.4 ActorRuntime 분해 ✅
- `run()` → 순수 메시지 루프 `processBatch()`(라이프사이클·예외만) + 측정/메트릭/재디스패치 래퍼로 분리
- mailbox → `unique_ptr<IMailbox>` 주입 (`runtime/mailbox/mailbox.hpp`가 `LockFreeMpscQueue<Message>` 래핑)
- 라이프사이클(`tryConsumeLifecycle`/`performRestart`)은 private 인라인 유지 — 별도 `ILifecycleHandler` 신설 안 함

### 1.5 전역 상태 제거: Metrics, Log, MemoryPool ✅
- `Metrics`/`Log` → 상태는 인스턴스, 접근은 전역 활성 핸들 `activeMetrics()`/`activeLogger()` (+fallback). `ILogger`/`IMetrics` 인터페이스 없음
- `Message` → `IMemoryAllocator*` 주입 (`MemoryPool::instance()` 제거, `defaultMemoryPool()` 폴백). `Message::make`/ops에 allocator 스냅샷
- payload-전용 send API: `sendMsg("target", CmdRequest{...})` — `Message` 생성 은닉 (기존 `Message` 전달 호출은 non-template 오버로드로 호환)
- `V2_ASSERT`는 개발용(NDEBUG 제거), 런타임 필수 실패는 assert 대신 리턴/전파, 복구 불가 시에만 `V2_PANIC`
- 잔여: `memory_pool.hpp`의 전역 `inline thread_local poolCaches`(thread_local_cache) — Phase 3.3에서 정리

### 1.6 서비스 계층 포트 소유권 정리 🔶 (남은 작업)

생성자 주입·composition root 이관은 완료. 남은 것은 포트 파일 이동과 nlohmann 제거.

**파일 이동** (인터페이스만 — 구현체는 infra 유지, header-only 유지):
- `src/infra/hal/pmu/i_pmu.hpp` → `src/service/ports/i_pmu.hpp`
- `src/infra/hal/sys/i_sys.hpp` → `src/service/ports/i_sys.hpp`
- `src/infra/hal/i2c/i_i2c.hpp` → `src/service/ports/i_i2c.hpp`

- [ ] 포트 3개 이동 + include 경로 수정 (소비자: `cmd_actor.hpp`, `monitor_actor.hpp`, `main_app.hpp` / 구현체: `pmu_rsp5`, `sys_linux`, `i2c_linux`)
- [ ] `II2c`는 소비자 0건(dead) — 이동만 수행

**nlohmann 제거** (service의 3rd-party 직접 의존 해소):
- [ ] `monitor_data.hpp`/`monitor_actor.cpp`의 `<nlohmann/json.hpp>` + `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` 제거 → 순수 POCO
- [ ] 직렬화는 `infra/transport/monitor_json_serializer.hpp/.cpp` 신설로 위임 (JSON 키 = 기존 필드명 유지 → TUI/CLI 호환)
- [ ] `MonitorActor` 생성자에 `std::function<std::string(const MonitorSnapshot&)> serializer` 주입, `main_app`에서 배선

**검증**: `rg 'infra/hal/pmu|infra/hal/sys|infra/hal/i2c' src/service` → 0, `rg 'nlohmann' src/service` → 0

**보류 (별도 종합 설계)**: `monitor_actor`의 `UdsServer` 직접 사용 — `ipc_server_actor`(UDS), `system_actor`(signal_handler)까지 포함한 "service↔infra 직접 include 0개"를 하나의 설계로 처리

### 1.7 서비스 전용 메시지 이관 ✅
- `cmd/ipc/dbus/monitor/tick/device_manager/network_manager` 메시지 → 각 소유 서비스 폴더로 이관 완료
- core `messages/`에 `message.hpp`/`message_traits.hpp`/`system_messages.hpp` 3개만 남음
- 참고: `MessageId` enum(서비스 ID 포함)은 core `message_traits.hpp`에 유지. 서비스 메시지 추가 시 core enum 수정이 필요하지만, ID 정의 이동은 과설계 판단으로 하지 않음

---

## Phase 2: 인프라 구현체 이관 (완료) — **P1 High**

### 2.1 플랫폼별 구현체 이관 ✅
- `EventLoopEpoll` → `infra/platform/linux/event_loop_epoll.hpp/cpp` (1.1에서 완료)
- `LinuxTimer`(timerfd) → `infra/platform/linux/timer_linux.hpp/cpp` (1.1에서 완료)
- `signal_handler`/`epoll` → `infra/platform/linux/` (이관 완료)
- 메모리: 기본 portable 풀(`MemoryPoolT`)은 core 유지, infra에는 특수/외부 할당자만 — 현재 추가할 특수 전략 없음 (YAGNI)

### 2.2 `worker.cpp`의 pthread 분리 ✅ (네이밍 제거로 해결)

core의 유일한 POSIX 호출(`worker.cpp`의 `pthread_setname_np`)을 제거한다.

**결정**: 스레드 이름을 주입(composition root)하는 대신 **워커 네이밍 자체를 제거**했다.

- 스레드 이름은 코드가 소비하지 않는 순수 디버깅(외부 도구)용이며, 워커는 동질(`runLoop`)이라 구분 가치가 낮음
- `std::thread`에는 이름 설정 API가 없어(OS 전용 `pthread_setname_np`/`SetThreadDescription`) std-only로는 불가 → "이름 유지 + 주입"이 6개 파일 설계인 데 비해, 제거는 5줄로 M5 목표 달성
- `v2-main`(event loop) 이름은 infra `event_loop_epoll.cpp`가 소유 — 그대로 유지되어 스레드 구분이 완전히 사라지지 않음
- `infra/threading/posix_thread.hpp/cpp` placeholder는 미사용 → 폐기

- [x] `worker.cpp`에서 `#include <pthread.h>` + `pthread_setname_np`(Linux/macOS 분기) 제거
- [x] `worker.hpp`에서 `threadName_` 멤버 제거
- [x] **검증**: `rg 'pthread|setname|threadName' src/core` → 0, 빌드 + `ctest` 132/132 통과

---

## Phase 2.5: 데이터 소유자 액터 + 구독(발행/구독) 정책 — **P0 (2026-08-09 설계 확정)**

> **목표**: ISys/IPmu 데이터 수집을 소유 액터로 이동하고, 소비자가 발행-구독으로 데이터를 받도록 재구성.
>
> **동기**: 현재 `MonitorActor`가 ISys/IPmu/UdsServer/직렬화를 모두 소유해 수신자와 무관하게 500ms마다 수집·직렬화(PMU vcgencmd 비용)하고, `CmdActor`가 IPmu를 직접 소유해 service가 infra에 직접 의존한다.
>
> **결정**: 요청/응답 correlation(replyTo) 도입 없이 **구독 정책만**으로 1:1/1:N/N:N을 모두 처리한다. 구독 시 최신 캐시를 즉시 발행(retained-latest)하여 on-demand 조회도 커버한다. 구독자가 없으면 수집 자체가 중단되는 **수요 기반 캐스케이드** 구조로 절약한다.

### 데이터 흐름 (확정)

```
[발행 500ms] SystemActor           — 구독자 있으면 ISys.collect() → SysDataUpdate{SystemResources}
[발행 500ms] DeviceManagerActor    — 구독자 있으면 IPmu.readPmuData() → PmuDataUpdate{PmuData}
                                    (PMU 소유자로 재정의 — devices_ 레지스트리 폐기)

MonitorActor (순수 집계자 — ISys/IPmu/UdsServer/직렬화 전부 제거)
  · 구독자 0→1: SysDataSubscribe + PmuDataSubscribe 전송 (수요 캐스케이드)
  · 구독자 1→0: 구독 해제 + 캐시 클리어
  · Sys/PmuDataUpdate 도착 시 캐시 갱신 → 구독자 있으면 스냅샷 조립+발행 (actor info는 조립 시점 수집)
  · event-driven — 자체 발행 타이머 없음

MonitorBridgeActor (신규 — UdsServer + serializer 소유)
  · 첫 소켓 연결: MonitorSubscribe{"monitor_bridge"} / 마지막 해제: MonitorUnsubscribe
  · MonitorSnapshotUpdate 수신 → clientCount 재기입 → serializer → 소켓 전송

CmdActor (IPmu 제거)
  · pmu status: PmuDataSubscribe → "Reading..." 즉시 응답 → 첫 PmuDataUpdate → CmdResponse → 구독 해제
```

**핵심 규칙**: (1) 발행은 구독자가 있을 때만 수집 — TUI/CLI가 없으면 sys/pmu 수집이 멈춤. (2) 구독 시 최신 캐시 즉시 발행 — on-demand 즉답. (3) 발행자별 `subscribers_` 집합 하나로 1:1/1:N/N:N 커버.

### 작업 목록

**메시지**
- [ ] `message_traits.hpp`: `DeviceRegister/Unregister/Enumerate/DeviceList` 제거, 구독·업데이트 9개 추가 (`SysData*` 3, `PmuData*` 3, `Monitor*` 3)
- [ ] `service/system/system_messages.hpp` 신규: `SysDataSubscribe{subscriber}`, `SysDataUnsubscribe{subscriber}`, `SysDataUpdate{SystemResources}`
- [ ] `service/device_manager/device_manager_messages.hpp` 재작성: `HalType`/`Device*` 제거, `PmuDataSubscribe/Unsubscribe/Update{PmuData}`
- [ ] `service/monitor/monitor_messages.hpp`: `MonitorSubscribe/Unsubscribe/MonitorSnapshotUpdate{MonitorSnapshot}` 추가 (NewConnection/Disconnected는 브리지가 사용)

**데이터 타입 소유권 분리 (POCO)**
- [ ] `service/system/system_data.hpp` 신규: `SystemResources` (nlohmann 없음)
- [ ] `service/device_manager/pmu_data.hpp` 신규: `PmuData` (nlohmann 없음)
- [ ] `service/monitor/monitor_data.hpp`: `ActorInfo`+`MonitorSnapshot`만 유지, 위 두 타입 include, `<nlohmann/json.hpp>` 제거

**액터**
- [ ] `SystemActor`: 생성자 `(name, id, ISys*, int pollIntervalMs)`, `SysDataTick` 타이머 + `subscribers_` + `latestSys_` 캐시, 구독 시 즉시 수집+발행 (시그널 처리 유지)
- [ ] `DeviceManagerActor`: 생성자 `(name, id, IPmu*, int pollIntervalMs)`, `devices_`/`DeviceEntry` 제거, `PmuDataTick` — SystemActor와 동일 패턴
- [ ] `MonitorActor`: 생성자 `(name, id)`로 축소, ISys/IPmu/UdsServer/직렬화 제거, 캐시 기반 event-driven 집계·재발행
- [ ] `MonitorBridgeActor` 신설: `ipc_server_actor`의 UdsServer/이벤트루프 패턴, serializer를 `std::function<std::string(const MonitorSnapshot&)>`로 주입, `#if V2_PLATFORM_LINUX` 가드
- [ ] `CmdActor`: `IPmu*` 제거, `dispatch()`에 `conn` 전달, `pmu status` 비동기(구독→응답→해제, `pendingPmuConn_`/`awaitingPmuStatus_`)

**포트/직렬화 (Phase 1.6 흡수)**
- [ ] 포트 3개 이동: `infra/hal/{pmu,sys,i2c}/i_*.hpp` → `service/ports/` (include 경로 갱신: `pmu_rsp5`, `sys_linux`, `i2c_linux`, `main_app`) — 1.6 목록 참조
- [ ] `infra/transport/monitor_json_serializer.hpp/.cpp` 신설 (JSON 키 기존 필드명 유지 → TUI/CLI 호환), 브리지에 주입
- [ ] `config/v2_main.json`: `enable_device_manager` `false → true`; `monitorPollIntervalMs`를 sys/pmu 발행 주기로 재사용 (신규 필드 없음)
- [ ] `main_app.cpp` 배선: `SystemActor`/`DeviceManagerActor`/`MonitorActor`/`MonitorBridgeActor`/`CmdActor` 생성자 갱신

**검증**
- [ ] `ctest` 전체(132) 통과
- [ ] `rg 'infra/hal/pmu|infra/hal/sys' src/service` → 0, `rg 'nlohmann' src/service` → 0
- [ ] 수동: `v2-main` + `v2 pmu status`(비동기 응답) + `v2 -m` TUI — 연결 시 수집 시작 / 해제 시 수집 중단 로그 확인

### 이 결정으로 해소되는 보류 항목

- 1.6의 **"보류: `monitor_actor`의 `UdsServer` 직접 사용"** → `MonitorBridgeActor` 분리로 해결 (service↔infra 직접 include 0개 달성)

### 트레이드오프 (기록)

- 모니터 event-driven 재발행 → 소켓 트래픽 최대 2× (sys+pmu 동시 발행 시)
- `pmu status` **last-wins**: 복수 CLI 동시 요청 시 마지막 conn으로 응답 (기존 wifi 패턴과 동일 한계)
- 스냅샷 내 sys/pmu 데이터 skew 최대 1 발행주기(500ms)
- `clientCount`: 브리지가 수신 스냅샷에 재기입 (모니터가 소켓을 모르므로)

---

## Phase 3: 성능 병목 해소 및 품질 향상 (2주) — **P1-P2**

### 3.1 Scheduler O(N) 정리 (벤치 확인 후 진행)

**파일**: `src/core/actor_system/runtime/scheduler/scheduler.cpp`

`cleanupTimerCtxs()`는 `addTimer`마다 전체 `timerCtxs_`를 순회하는 O(N) 지점. 지금은 타이머 개수가 소수라 실측 병목이 아닐 수 있으므로 `bench/bench_scheduler`로 먼저 확인한 뒤 진행한다.

```cpp
// 현재: 매 addTimer마다 전체 맵 순회 (scheduler.cpp:26,41)
void Scheduler::cleanupTimerCtxs() {
    for (auto it = timerCtxs_.begin(); it != timerCtxs_.end(); ) {
        if (!timer_->isAlive(it->first)) it = timerCtxs_.erase(it);
        else ++it;
    }
}
```

- [ ] `bench/bench_scheduler`로 병목 실측
- [ ] 병목 확인 시: 만료 처리 경로에서 즉시 제거, 또는 `CLEANUP_THRESHOLD` 기반 지연 정리 플래그

### 3.2 ActorRegistry 락 분할

**파일**: `src/core/actor_system/actor/actor_registry.hpp/cpp`

```cpp
// Before: 단일 std::mutex로 모든 연산 직렬화 (actor_registry.hpp:36)
mutable std::mutex mutex_;

// After: 읽기/쓰기 분리 (C++17 std::shared_mutex — 외부 의존 없음)
mutable std::shared_mutex mutex_;
// findByName/forEachActor: shared_lock, add/remove: unique_lock
// forEachActor는 락 해제 후 콜백 실행 (스냅샷 후 콜백 — 콜백 내 registry 수정 방지)
```

- [ ] `std::shared_mutex` 전환 + read/write 락 배분, `forEachActor` 스냅샷 패턴 적용

### 3.3 전역 `thread_local` 정리 (tlCache_)

**파일**: `src/core/common/memory/thread_local_cache.hpp`, `memory_pool.hpp`

`memory_pool.hpp:92`의 전역 `inline thread_local std::array<ThreadLocalCache, kMaxPools> poolCaches`가 남은 전역 상태다. C++에서 비정적 멤버에 `thread_local`을 붙일 수 없어, "풀 인스턴스 소유 thread cache"는 `thread_local` 레지스트리(예: `map<풀*, 캐시>`) 재설계가 필요하다.

- [ ] `poolCaches` 전역 제거 → 풀 인스턴스별 캐시 소유 구조로 재설계 (hot path 성능 회귀 확인)

---

## Phase 4: 도메인 모델 풍부화 및 마무리 (1-2주) — **P2**

### 4.1 설정 필드 일반화

**파일**: `src/core/common/config/runtime_config.h`

`runtime_config.h:30-31`의 `epollMaxEvents`/`epollWaitTimeoutMs`는 infra 전용 의미(`epoll`)가 core 설정에 노출되어 있다. 이벤트루프는 주입 대상이므로 `eventLoop*`로 일반화한다. JSON 직렬화는 `infra/config/json_config_loader.cpp`(nlohmann 접촉 지점 유일)가 담당하며 core에 nlohmann이 없다.

```cpp
// runtime_config.h — 필드명만 교체
#if V2_PLATFORM_LINUX
    int eventLoopMaxEvents = 64;      // 기존 epollMaxEvents
    int eventLoopWaitTimeoutMs = 1000; // 기존 epollWaitTimeoutMs
#endif
```

- [ ] 필드명 교체 + 참조처(`json_config_loader.cpp`, app) 갱신

### 4.2 최종 검증 및 문서화

- [ ] 전체 테스트 스위트 실행 (단위/통합 모두 통과)
- [ ] 클린 아키텍처 검증: `core/`에 OS/라이브러리 의존 코드 0건, `v2_core` 링크에 외부 라이브러리 없음
- [ ] 성능 벤치마크 전/후 비교 (latency, throughput, contention)
- [ ] Doxygen API 문서 생성 (인터페이스 위주)
- [ ] 마이그레이션 가이드 작성

---

## 마일스톤 및 체크포인트

| 마일스톤 | 상태 | 검증 기준 |
|----------|------|-----------|
| **M1: 인터페이스 완성** | ✅ | 핵심 인터페이스 정의 완료, 컴파일 통과 (1.1~1.2) |
| **M2: DI + ActorSystem 주입** | ✅ | `ActorSystem` 생성자 주입 동작, 기존 테스트 통과 (1.3) |
| **M3: ActorRuntime 분해** | ✅ | `run()` = `processBatch` + 계측/재디스패치 래퍼, `IMailbox` 주입 (1.4) |
| **M4: 전역 상태 제거** | ✅ | `Metrics`/`Log`/`MemoryPool` static/전역 싱글톤 제거 (1.5, `tlCache_`는 3.3) |
| **M5: 인프라 이관 완료** | ✅ | core에 OS 의존 0건 — worker 스레드 네이밍 제거로 해결 (2.2) |
| **M5b: 서비스 계층 경계 복원** | 🔶 | 포트 이관 + nlohmann 제거 (1.6). 메시지 이관은 완료 (1.7) |
| **M5c: 데이터 소유자 + 구독 정책** | 🔶 | sys/pmu 소유 액터, MonitorBridgeActor, 구독 기반 데이터 흐름 (2.5). 1.6 보류(UdsServer) 해소 |
| **M6: 성능 병목 해소** | ⏳ | Scheduler 정리(벤치 확인 후), Registry 락 분할 (3.1~3.2) |
| **M7: 도메인 모델 정리** | ⏳ | 설정 필드 일반화 (4.1) |
| **M8: 최종 릴리스** | ⏳ | 전체 테스트 통과, 문서화 완료, 성능 회귀 없음 |

---
