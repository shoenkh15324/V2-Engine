# V2-Engine Core Layer 리팩토링 로드맵

> **목표**: Core Layer와 Service Layer를 Clean Architecture 원칙에 부합하는 순수 계층으로 재구축 (포트 소유권 정리 포함)
> **기간**: 약 7-10주 (Phase 1-4)
> **기준선**: 현재 총점 20/70 → 목표 60+/70

---

## Phase 0: 준비 및 인프라 (1주)

### 0.1 브랜치 전략 및 CI 설정 (생략)
- [ ] `refactor/core-architecture` 브랜치 생성
- [ ] GitHub Actions / GitLab CI에 **클린 빌드 + 단위 테스트** 파이프라인 추가
- [ ] `compile_commands.json` 생성 설정 (clang-tidy, cppcheck 연동)
- [ ] **베이스라인 메트릭 수집**: 현재 테스트 커버리지, 빌드 시간, 바이너리 크기 기록

### 0.2 인터페이스 정의 디렉토리 구조 생성 ✅

> **원칙 (Phase 0 결정사항)**
> - **의존 방향**: `core`(내부) → `infra`(외부). core는 OS/외부 라이브러리를 알지 못함
> - core는 **C++20 (프로젝트 전체 통일), std(+libc)만 사용**, 외부 라이브러리 링크 금지, **단독 빌드/실행 가능** (게임 엔진 코어 모델)
> - **기존 `i_*` 파일이 이미 Port** — 신규 인터페이스는 `ITimeSource`, `IMemoryAllocator` **두 개만 추가** (과설계 방지)
> - **Port는 각 도메인 폴더에 co-location** (별도 `common/interfaces/` 디렉토리 없음 — 기존 `i_actor_registry` 등과 동일 규칙)
> - **OS 의존 구현체만 infra로 이관**: epoll, timerfd, pthread, signal_handler (타이머는 portable(스레드+세마포어) 구현을 core `common/timer`에 유지하고, Linux 최적화인 timerfd만 infra `LinuxTimer`가 오버라이드)
> - 네이밍: `IClock` → **`ITimeSource`** (CPU 클럭 혼동 방지), `IAllocator` → **`IMemoryAllocator`** (할당 대상 명시)
>
> **추가 원칙 (Phase 1 보강)**
> - **포트 소유권 = 소비자 소유 (DIP)**: 인터페이스는 **사용하는 계층이 소유**, infra는 구현체만 소유
>   - 예: `IPmu`/`ISys`/`II2c`는 소비자(`cmd`, `monitor`)가 있는 `service/ports/`로 이전. 구현체(`PmuRsp5`, `SysLinux`)는 infra 유지
>   - 인터페이스가 공급자(infra)에 있으면 의존 화살표가 `service → infra`로 새므로 위반. 인터페이스 위치가 곧 의존 방향
> - **메시지도 co-location**: 서비스 전용 메시지(`dbus`, `wifi`, `cmd`, `ipc`, `monitor`, `tick`, `device_manager`)는 소유 서비스 폴더로. core `messages/`는 엔진 범용 타입만
> - **Composition Root는 app**: 구체 타입 생성/주입은 app에서만. core/service에서 infra 구체 타입 `#include` 및 `new` 금지
> - **플랫폼 분기 금지**: service에서 `#if V2_PLATFORM_*`로 구현체 선택 금지 → 생성자 주입/팩토리로 위임 (하드웨어 결정은 배포 문제이지 비즈니스 로직 문제가 아님)
> - **service는 core + 자체 포트만 의존**: infra 및 3rd-party(`nlohmann_json`, `sdbus` 등) 직접 참조 금지
> - **서비스 간 결합은 메시지로**: 구체 액터 헤더 직접 include 대신 메시지/레지스트리 조회
> - **빌드 시스템이 경계를 강제**: CMake 타깃 의존성으로 계층 위반을 컴파일/링크 단계에서 검출. 의존 방향은 **안쪽으로만**: `v2_app → v2_infra/v2_service → v2_core` (각 레이어는 자신보다 안쪽 레이어만 링크, **`v2_core`는 아무것도 링크하지 않음**). core를 독립 subproject로 분리해 단독 빌드/실행을 CI에서 증명 (1.1)

```
src/core/                                  # 내부 원: 도메인 (Entities + Use Cases)
│                                          # C++20, std-only, 단독 빌드/실행
├── actor_system/
│   ├── actor/                             # [Entities] 액터 신원·참조·조회
│   │   ├── actor.hpp/cpp
│   │   ├── actor_handle.hpp/cpp
│   │   ├── i_actor_registry.hpp           # MOVE: runtime/ → actor/ (묶음화)
│   │   └── actor_registry.hpp/cpp         # MOVE: runtime/ → actor/
│   ├── messages/                          # [Entities] 메시지 계약
│   │   ├── message.hpp/cpp
│   │   └── message_traits.hpp
│   ├── runtime/                           # [Use Cases] 처리 오케스트레이션
│   │   ├── actor_runtime/
│   │   │   ├── actor_runtime.hpp/cpp
│   │   │   └── i_actor_runtime.hpp            # 유지 (Port, co-location)
│   │   ├── scheduler/
│   │   │   ├── i_scheduler.hpp                # 유지 (Port, co-location)
│   │   │   └── scheduler.hpp/cpp              # 유지 (ITimer 주입 — null이면 core portable Timer 기본)
│   │   ├── dispatcher/
│   │   │   ├── worker.hpp/cpp             # pthread 호출은 infra로 분리
│   │   │   ├── i_work_dispatcher.hpp      # 유지 (Port)
│   │   │   ├── work_dispatcher.hpp/cpp
│   │   │   └── io/
│   │   │       └── i_event_loop.hpp       # 유지 (Port, 도메인 co-location)
│   │   └── supervisor/
│   │       ├── i_supervisor.hpp           # 유지 (Port)
│   │       ├── supervisor.hpp/cpp
│   │       ├── i_supervised.hpp
│   │       └── dead_letter_queue.hpp/cpp
│   └── actor_system.hpp/cpp               # 엔진 조립 API (createDefault는 편의 팩토리, 실배선은 app에서)
├── common/                                # 공용 도메인 — std(+libc) 전용, OS/3rd-party 무의존
│   ├── config/                            # 설정 POCO 타입 + 컴파일타임 탐지만
│   │   ├── platform_config.h              # 유지 — V2_PLATFORM_*/V2_COMPILER_* 탐지 (OS 호출 없음)
│   │   ├── runtime_config.h               # 유지 — 설정 구조체만 (epoll 필드명은 eventLoop로 일반화)
│   │   └── runtime_config.cpp             # 삭제 완료 ✅ (nlohmann 파싱 → infra/config/json_config_loader.cpp)
│   ├── container/                         # std-only 순수 컨테이너 (인터페이스도 core co-location)
│   │   ├── cache_line.hpp                 # 유지 — 캐시라인 정렬
│   │   ├── lock_free_mpsc_queue.hpp       # 유지 — MPSC 메일박스 큐 (구현체)
│   │   └── ring_buffer.hpp/cpp            # 유지 — 바이트 링 버퍼 (IByteBuffer 분리는 Phase 4.4)
│   ├── time/                              # 시간 도메인
│   │   ├── time.hpp/cpp                   # 유지 — 순수 수치 변환 + now() (std::chrono)
│   │   └── sleep.hpp                      # 유지 — MOVE 완료 (util/ → time/)
│   ├── timer/                             # 타이머 도메인 (1.1 신설)
│   │   ├── i_timer.hpp                    # NEW (1.1) — ITimer 포트 (co-location, portable 기본)
│   │   ├── timer_base.hpp/cpp             # NEW (1.1) — 타이머 공통 힙/풀/콜백 로직 (std-only)
│   │   └── timer.hpp/cpp                  # portable Timer (스레드+세마포어), core 기본값
│   ├── memory/                            # 메모리 도메인 — 순수 구현
│   │   ├── i_memory_allocator.hpp         # NEW (IAllocator → IMemoryAllocator) — Port
│   │   ├── memory_pool.hpp                # 유지 — MemoryPoolT 순수 구현
│   │   ├── slab.hpp / size_class.hpp      # 유지 — slab 할당기 / 크기 클래스
│   │   ├── chunk.hpp / free_list.hpp      # 유지 — 청크 / 자유 목록
│   │   └── thread_local_cache.hpp         # 유지 — TLC 캐시
│   ├── log/                               # 로깅 도메인
│   │   ├── i_logger.hpp                   # NEW (Phase 1.2.3) — Port (ILogger)
│   │   └── log.hpp/cpp                    # 유지 (fprintf/std — std-only 기본 구현)
│   └── util/                              # 공용 유틸 (std-only)
│       ├── return.hpp                     # 유지 — Ok/Fail + Result<T>
│       └── debug.hpp                      # 유지 — V2_ASSERT (iostream)
└── perf/metrics/                          # [Entities] Metrics → 인스턴스 기반 (Phase 1.5)

src/infra/                                 # 외부 원: Adapters — OS/외부 라이브러리 의존만
├── platform/linux/
│   ├── event_loop_epoll.hpp/cpp           # MOVE: dispatcher/io/ (IEventLoop 구현)
│   ├── timer_linux.hpp/cpp                # NEW (1.1) — LinuxTimer (timerfd, core TimerBase 파생) — portable 대체 최적화
│   └── signal_handler.hpp/cpp             # MOVE: common/os/
├── threading/
│   └── posix_thread.hpp/cpp               # worker의 pthread_setname_np 분리
├── memory/
│   └── memory_pool_allocator.hpp/cpp      # IMemoryAllocator 구현 (MemoryPoolT 어댑터)
├── config/json_config_loader.hpp/cpp      # nlohmann_json 의존
├── ui/ftxui_renderer.hpp/cpp              # ftxui 의존
└── mock/                                  # MockTimeSource, MockAllocator, TestRegistry

src/service/                             # Use Cases — 비즈니스 로직 (core + 자체 포트만 의존)
├── device_manager/
│   ├── device_manager_actor.hpp/cpp
│   └── device_manager_messages.hpp       # MOVE: core/messages/ → 여기
├── network_manager/
│   ├── network_manager_actor.hpp/cpp
│   ├── network_manager_messages.hpp      # MOVE: core/messages/network_manager/ → 여기
│   └── wifi_messages.hpp                 # MOVE: core/messages/network_manager/ → 여기
├── monitor/
│   ├── monitor_actor.hpp/cpp
│   ├── monitor_messages.hpp              # MOVE: core/messages/ → 여기
│   └── monitor_data.hpp                  # 유지 (공유 POCO)
├── dbus/
│   ├── dbus_actor.hpp/cpp
│   ├── dbus_messages.hpp                 # MOVE: core/messages/ → 여기
│   └── i_dbus_handler.hpp                # Port — 구현(sdbus)은 infra
├── ipc/
│   ├── ipc_server_actor.hpp/cpp
│   ├── ipc_messages.hpp                  # MOVE: core/messages/ → 여기
│   └── i_ipc_server.hpp                  # Port — 구현(UdsServer)은 infra
├── cmd/
│   ├── cmd_actor.hpp/cpp
│   └── cmd_messages.hpp                  # MOVE: core/messages/ → 여기
├── system/
│   └── system_actor.hpp/cpp              # signal_handler 호출은 infra로
├── tick/
│   ├── tick_actor.hpp/cpp
│   └── tick_messages.hpp                 # MOVE: core/messages/ → 여기
└── ports/                                # 서비스 소유 포트 (구현체는 infra)
    ├── i_pmu.hpp                         # MOVE: infra/hal/pmu/ → 여기
    ├── i_sys.hpp                         # MOVE: infra/hal/sys/ → 여기
    └── i_i2c.hpp                         # MOVE: infra/hal/i2c/ → 여기

bench/                                     # 벤치마크 (core의 소비자)
app/                                       # CLI/TUI/main — Composition Root (기존)
```

### 0.3 의존성 주입 컨테이너 도입 (경량) ✅
- [ ] `core/common/di/` 에 `ServiceContainer`, `ServiceProvider` 구현
- [ ] 생성자 주입만 지원 (setter 주입 지양)
- [ ] 컴파일 타임 바인딩 우선, 런타임 오버라이드 허용

---

## Phase 1: 아키텍처 경계 복원 (2-3주) — **P0 Critical**

### 1.1 Core CMake 정리 + 독립 subproject 분리 (P0-1)

> **현재 상태 (1.1.1 완료)**: `v2_core` 소스의 infra/`nlohmann`/`ftxui` 참조는 전부 제거됨 (`rg 'infra/|nlohmann/|ftxui/' src/core` → 0건). 남은 위반은 `v2_core`가 **`v2_infra`를 PUBLIC으로 링크**하는 core.cmake의 잔재 링크 1줄뿐 (1.1.2에서 제거).
> **목표**: `v2_core` 링크를 완전히 비우고 core를 **독립 CMake subproject**로 분리해 "단독 빌드/실행 가능" 원칙을 빌드가 강제하게 한다. (`add_library(v2_core INTERFACE)` 전환은 core에 `.cpp` 구현이 남아 있어 **보류** — Phase 2 이후 검토). **타이머 결정 (구현 완료)**: portable(스레드+세마포어) `Timer`를 core `common/timer`에 두어 core 단독 빌드에서도 **메시징+타이머가 동작**하게 하고, Linux 최적화(timerfd)는 infra `LinuxTimer`가 오버라이드. 선택은 Composition Root(app)가 함 — **"epoll 주입 = Linux 감지" → `LinuxTimer` 동반 주입**. core는 "주입 or 자기 기본"만 가짐.

#### 1.1.1 core의 infra/외부 의존 제거

| 의존 | 원인 | 조치 |
|---|---|---|
| `nlohmann_json` | `runtime_config.cpp` JSON 파싱 | ✅ 완료 — 파싱 → `infra/config/json_config_loader.cpp` 이관, `runtime_config.cpp` 삭제 (선언은 core `runtime_config.h`에 유지 — 4.2 패턴) |
| `Timer` (timerfd) | `scheduler.hpp`가 infra 구체 타입 멤버 보유 | ✅ 완료 — `ITimer` 포트 + portable `Timer`를 core `common/timer`에 둠(스레드+세마포어), `Scheduler`가 `unique_ptr<ITimer>` 주입. infra `LinuxTimer`(timerfd, `TimerBase` 파생)가 Linux에서 대체 |
| `EventLoopEpoll` | `actor_system.hpp`가 구체 타입 멤버 보유 | ✅ 완료 — `IEventLoop`에 `start/run/stop`(`override` 명시), `ActorSystem`이 `unique_ptr<IEventLoop>` 주입받음 |

- [ ] `RuntimeConfig::loadFromFile` 본문 → `infra/config/json_config_loader.cpp` 이동 ✅
- [ ] `src/core/common/config/runtime_config.cpp` 삭제 ✅
- [ ] `core/common/timer/i_timer.hpp` — `ITimer` 신규 (common/timer co-location; 1.2.6의 `ITimerService`로 승격 예정) ✅
- [ ] `core/common/timer/timer_base.hpp/cpp` — 타이머 공통 힙/풀/콜백 로직 추출 (`TimerBase : ITimer`, std-only) ✅
- [ ] `core/common/timer/timer.hpp/cpp` — portable `Timer` 복원 (스레드+세마포어; 기존 infra `timer_fd.cpp` `#else` 브랜치 이관), core 기본값 ✅
- [ ] `Scheduler` → `ITimer*` 생성자 주입 (null → core portable `Timer` 기본 생성), infra include 제거 ✅
- [ ] `EventLoopEpoll`의 `start()/run()/stop()`에 `override` 명시 (`IEventLoop`에는 이미 존재) ✅
- [ ] `ActorSystem` → `std::unique_ptr<IEventLoop>` + `std::unique_ptr<ITimer>` 생성자 주입 (null → core portable 기본; 구체 생성은 Composition Root=app) ✅
- [ ] `infra/platform/linux/timer_linux.hpp/cpp` — `LinuxTimer : TimerBase` (timerfd; 기존 Linux 브랜치 이관) ✅
- [ ] Composition Root(main_app) — Linux면 `LinuxTimer` + `EventLoopEpoll` **동시 주입** ("epoll 주입 = Linux 감지" 규칙). core는 LinuxTimer를 모름 ✅

#### 1.1.2 독립 subproject 분리

```cmake
# src/core/CMakeLists.txt (독립 project)
cmake_minimum_required(VERSION 3.14.0)
project(v2_core LANGUAGES CXX)

add_library(v2_core OBJECT ...)          # std(+libc)만
target_include_directories(v2_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../)   # core/... 접두어 유지
# 링크 없음 — 외부 라이브러리 금지
find_package(Threads REQUIRED)           # std::thread/pthread (std 계열)
target_link_libraries(v2_core PUBLIC Threads::Threads)
add_executable(v2_core_smoke standalone/main.cpp)   # 단독 빌드/실행 증명
```

- [ ] `src/core/CMakeLists.txt` 신규 → root에서 `include(core.cmake)` → `add_subdirectory(src/core)` 전환
- [ ] `src/core/standalone/main.cpp` smoke 타깃 — 모든 core 오브젝트 + trivial main을 링크해 "외부 심볼 무참조" 증명 (매 빌드 경계 강제). core portable `Timer` + std-only mock/블로킹 루프를 주입해 **메시징 + 타이머를 실제 실행** (fd 기능은 데모 범위 밖)
- [ ] `v2_core`의 compile definitions(`V2_ENGINE_NAME/VERSION/V2_CONFIG_DIR`) → `app.cmake`로 이관 (전부 app에서만 사용됨)
- [ ] `V2_CONFIG_DIR` 경로 버그 수정: 현재 `src/core/config`(존재하지 않음 → config 미로딩) → `${CMAKE_SOURCE_DIR}/config`
- [ ] 의존 방향 교정: `v2_infra`에 `target_link_libraries(v2_infra PUBLIC v2_core)` 추가 **및 `v2_core`의 잔재 링크 `v2_infra` 제거** (core→infra 링크 제거)
- [ ] infra 의존 테스트(`test_timer`, `test_event_loop_epoll`, `test_scheduler`, `test_actor_system`, `test_actor_system_integration`, `test_timer_pipeline`)에 `v2_infra` 명시 ✅(`v2_bench`는 `v2_infra` 링크 완료 — bench.cmake)

#### 1.1.3 검증
- [ ] `rg 'infra/|nlohmann/|ftxui/' src/core` → 결과 없음 ✅
- [ ] standalone 빌드: `cmake -S src/core -B <dir> && cmake --build <dir>` 성공
- [ ] `v2_core_smoke` 링크 성공 (외부 심볼 무참조 증명)
- [ ] 전체 빌드 + `ctest` 전체 통과 ✅ (123/123)
- [ ] 순수 테스트(`test_ring_buffer` 등) 링크 라인에 `libv2_infra.a` 미포함

### 1.2 핵심 인터페이스 정의 (P0-2)

#### 1.2.1 `core/common/time/i_time_source.hpp` ~~— 삭제됨 (1.1에서 정리)~~
> `i_time_source.hpp`는 참조처가 없어 1.1에서 제거. `Time`(std::chrono 래퍼)만으로 충분. 신설 시 이 표준 코드 사용.
```cpp
#pragma once
#include <chrono>
#include <cstdint>

class ITimeSource {
public:
    virtual ~ITimeSource() = default;
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::steady_clock::duration;

    virtual TimePoint now() const = 0;
    virtual int64_t nowMs() const = 0;
    virtual int64_t nowUs() const = 0;
    virtual int64_t nowNs() const = 0;
    virtual TimePoint afterMs(int64_t ms) const = 0;
    virtual TimePoint afterUs(int64_t us) const = 0;
    virtual TimePoint afterNs(int64_t ns) const = 0;
};
```

#### 1.2.2 `core/common/memory/i_memory_allocator.hpp`
```cpp
#pragma once
#include <cstddef>
#include <cstdint>

class IMemoryAllocator {
public:
    virtual ~IMemoryAllocator() = default;
    virtual void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;
    virtual void deallocate(void* ptr, size_t size, size_t alignment = alignof(std::max_align_t)) = 0;
    virtual size_t allocatedBytes() const noexcept = 0;
    virtual size_t allocatedBlocks() const noexcept = 0;
};
```

#### 1.2.3 `core/common/log/i_logger.hpp`
```cpp
#pragma once
#include <string_view>
#include <cstdint>

enum class LogLevel : uint8_t { Verbose=0, Info=1, Warn=2, Error=3 };

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, std::string_view file, int line, std::string_view func, std::string_view msg) = 0;
    virtual void setLevel(LogLevel level) = 0;
    virtual LogLevel getLevel() const = 0;
    virtual void setOutputFile(std::string_view path) = 0;
};
```

#### 1.2.4 `core/perf/metrics/i_metrics.hpp`
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ActorMetricsSnapshot { uint64_t id; std::string name; uint64_t enqueued, processed, dropped, handleTimeNs, batches; size_t peakDepth; };
struct WorkerMetricsSnapshot { uint64_t batches, busyTimeNs, idleTimeNs, messages; };
struct DispatcherMetricsSnapshot { uint64_t dispatchCount, acquireCount; size_t readyQueuePeak; };

struct MetricsSnapshot {
    std::vector<ActorMetricsSnapshot> actors;
    std::vector<WorkerMetricsSnapshot> workers;
    DispatcherMetricsSnapshot dispatcher;
};

class IMetrics {
public:
    virtual ~IMetrics() = default;
    virtual void init(size_t numWorkers) = 0;
    virtual void registerActor(uint64_t actorId) = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;
    virtual void recordEnqueue(uint64_t actorId, bool success, size_t depth) = 0;
    virtual void recordHandle(uint64_t actorId, size_t count, uint64_t durationNs) = 0;
    virtual void recordBatch(int workerId, size_t msgCount, uint64_t busyNs, uint64_t idleNs) = 0;
    virtual void recordDispatch(size_t queueDepth) = 0;
    virtual void recordAcquire() = 0;
    virtual MetricsSnapshot snapshot() = 0;
    virtual void reset() = 0;
};
```

#### 1.2.5 `core/actor_system/runtime/i_mailbox.hpp`
```cpp
#pragma once
#include "core/actor_system/messages/message.hpp"

class IMailbox {
public:
    virtual ~IMailbox() = default;
    virtual bool push(Message&& msg) = 0;
    virtual bool pop(Message& out) = 0;
    virtual size_t count() const = 0;
    virtual size_t capacity() const = 0;
    virtual bool empty() const = 0;
    virtual void clear() = 0;
};
```

#### 1.2.6 `core/actor_system/runtime/i_timer_service.hpp`
```cpp
#pragma once
#include <cstdint>
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "core/actor_system/messages/message.hpp"

class ITimerService {
public:
    virtual ~ITimerService() = default;
    virtual int addTimer(IActorRuntime* target, Message msg, uint64_t delayMs, bool repeating) = 0;
    virtual void cancelTimer(int timerId) = 0;
    virtual void cancelAllTimers() = 0;
    virtual size_t timerCount() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};
```

#### 1.2.7 `core/actor_system/runtime/i_lifecycle_handler.hpp`
```cpp
#pragma once
#include "core/actor_system/actor/actor.hpp"
#include <string>

class ILifecycleHandler {
public:
    virtual ~ILifecycleHandler() = default;
    virtual bool handleLifecycle(Actor* actor, const Message& msg) = 0;  // true: 처리됨, false: 사용자 핸들러로 전달
    virtual void performRestart(Actor* actor, const std::string& reason) = 0;
};
```

#### 1.2.8 나머지 인터페이스들도 동일 패턴으로 정의
- `i_event_loop.hpp` — `subscribe`, `unsubscribe`, `run`, `stop`, `post`
- `i_work_dispatcher.hpp` — `dispatch`, `redispatch`, `acquire`, `start`, `stop`, `drainAndStop`, `beginDrain`, `isDraining`, `pendingWork`, `onWorkDone`
- `i_scheduler.hpp` — `ITimerService`와 통합 또는 별도
- `i_actor_registry.hpp` — 기존 `IActorRegistry` 유지
- `i_supervisor.hpp` — 기존 `ISupervisor` 유지
- `i_dead_letter_queue.hpp` — `push`, `pop`, `count`, `capacity`

### 1.3 ActorSystem 생성자 주입 리팩토링 (P0-3)

**파일**: `src/core/actor_system/actor_system.hpp`, `actor_system.cpp`

```cpp
// Before: 내부에서 모든 구현체 생성
class ActorSystem {
    std::unique_ptr<WorkDispatcher> workDispatcher_;
    std::unique_ptr<EventLoopEpoll> eventLoop_;
    std::unique_ptr<Scheduler> scheduler_;
    // ...
public:
    explicit ActorSystem(int numWorkers, int maxBatch = 32, int epollMaxEvents = 64, int epollWaitTimeoutMs = 1000);
};

// After: 인터페이스만 받고, 팩토리/컨테이너에서 주입
struct ActorSystemConfig {
    int numWorkers = 1;
    int maxBatch = 32;
    int epollMaxEvents = 64;
    int epollWaitTimeoutMs = 1000;
    size_t defaultMailboxSize = 512;
};

class ActorSystem {
    std::unique_ptr<IWorkDispatcher> workDispatcher_;
    std::unique_ptr<IEventLoop> eventLoop_;
    std::unique_ptr<IScheduler> scheduler_;
    std::unique_ptr<IDeadLetterQueue> deadLetterQueue_;
    std::unique_ptr<ISupervisor> supervisor_;
    IActorRegistry& actorRegistry_;  // 참조 (소유하지 않음)
    IMetrics* metrics_ = nullptr;
    ILogger* logger_ = nullptr;
    IClock* clock_ = nullptr;

public:
    ActorSystem(const ActorSystemConfig& config,
                std::unique_ptr<IWorkDispatcher> dispatcher,
                std::unique_ptr<IEventLoop> eventLoop,
                std::unique_ptr<IScheduler> scheduler,
                std::unique_ptr<IDeadLetterQueue> dlq,
                std::unique_ptr<ISupervisor> supervisor,
                IActorRegistry& registry,
                IMetrics* metrics = nullptr,
                ILogger* logger = nullptr,
                IClock* clock = nullptr);

    // 팩토리 메서드 제공 (편의용)
    static std::unique_ptr<ActorSystem> createDefault(const ActorSystemConfig& config);
};
```

- [ ] `ActorSystemConfig` 구조체로 매직 넘버 외부화
- [ ] `createDefault()` 팩토리에서 기본 구현체 생성 (나중에 `infra`에서 오버라이드 가능)
- [ ] 기존 테스트 코드 수정: `TestScheduler`, `TestRegistry` 등 mock 주입 가능하게

### 1.4 ActorRuntime 분해 (P0-4)

**파일**: `src/core/actor_system/runtime/actor_runtime.hpp/cpp` → 분해

#### 1.4.1 새로운 클래스 구조
```
ActorRuntime (핵심: 메시지 루프만 담당)
├── IMailbox* mailbox_                    // 메시지 큐 위임
├── ILifecycleHandler* lifecycle_         // open/close/restart 위임
├── ITimerService* timers_                // 타이머 위임
├── ISupervisor* supervisor_              // 실패 알림만
├── IActorRegistry* registry_             // 액터 조회만
├── IEventLoop* eventLoop_                // 이벤트 등록만
├── IWorkDispatcher* dispatcher_          // 재디스패치만
├── IMetrics* metrics_                    // 메트릭 기록만 (옵션)
├── ILogger* logger_                      // 로깅만 (옵션)
└── IClock* clock_                        // 시간 측정만 (옵션)
```

#### 1.4.2 `ActorRuntime::run()` 순수화
```cpp
// Before: 시간, 메트릭, 예외, 라이프사이클 모두 섞임
int ActorRuntime::run(int maxBatch, bool* moreWork) { ... }

// After: 순수 메시지 처리만
struct RunResult {
    int processed = 0;
    bool moreWork = false;
    uint64_t durationNs = 0;
    bool stopped = false;
};

RunResult ActorRuntime::runPure(int maxBatch) {
    if (stopped_.load(std::memory_order_relaxed)) return {0, false, 0, true};
    
    auto startTime = clock_ ? clock_->now() : TimePoint{};
    Message msg;
    int processed = 0;
    
    while ((maxBatch < 0) || (processed < maxBatch)) {
        if (!mailbox_->pop(msg)) break;
        
        if (!lifecycle_->handleLifecycle(actor_.get(), msg)) {
            try {
                actor_->handle(msg);
            } catch (const std::exception& e) {
                if (supervisor_) {
                    supervisor_->onActorFailed(this, std::move(msg), e.what());
                } else if (logger_) {
                    logger_->log(LogLevel::Error, __FILE__, __LINE__, __func__, 
                                std::format("Actor {} threw: {}", actor_->name(), e.what()));
                }
                return {processed, false, 0, false};  // 예외 발생 시 중단
            } catch (...) {
                if (supervisor_) {
                    supervisor_->onActorFailed(this, std::move(msg), "unknown exception");
                }
                return {processed, false, 0, false};
            }
        }
        processed++;
    }
    
    auto endTime = clock_ ? clock_->now() : TimePoint{};
    uint64_t durationNs = clock_ ? std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count() : 0;
    
    bool more = !mailbox_->empty();
    return {processed, more, durationNs, false};
}

// 외부 래퍼에서 메트릭/디스패치 처리
int ActorRuntime::run(int maxBatch, bool* moreWork) {
    auto result = runPure(maxBatch);
    if (metrics_) metrics_->recordHandle(actor_->id(), result.processed, result.durationNs);
    if (result.more && dispatcher_) {
        bool ok = dispatcher_->redispatch(this);
        if (moreWork) *moreWork = ok;
    } else if (moreWork) {
        *moreWork = false;
    }
    return result.processed;
}
```

#### 1.4.3 라이프사이클 핸들러 분리
```cpp
// src/core/actor_system/runtime/lifecycle_handler.hpp
class DefaultLifecycleHandler : public ILifecycleHandler {
public:
    bool handleLifecycle(Actor* actor, const Message& msg) override {
        switch (msg.id()) {
            case MessageId::ActorEnableRequest:
                if (actor->getState() == ActorState::Closed) actor->open();
                return true;
            case MessageId::ActorDisableRequest:
                if (actor->getState() == ActorState::Opened && !actor->isEssential()) actor->close();
                return true;
            case MessageId::ActorRestartRequest:
                if (actor->getState() == ActorState::Opened) {
                    performRestart(actor, msg.as<ActorRestartRequest>().reason);
                }
                return true;
            default:
                return false;
        }
    }
    
    void performRestart(Actor* actor, const std::string& reason) override {
        if (logger_) logger_->log(LogLevel::Info, __FILE__, __LINE__, __func__,
                                  std::format("Restarting actor {} reason: {}", actor->name(), reason));
        actor->close();
        if (actor->getState() == ActorState::Closed) actor->open();
    }
    
    void setLogger(ILogger* logger) { logger_ = logger; }
private:
    ILogger* logger_ = nullptr;
};
```

### 1.5 전역 상태 제거: Metrics, Log, MemoryPool (P0-5, P0-6)

#### 1.5.1 `Metrics` → 인스턴스 기반
```cpp
// src/core/perf/metrics/metrics.hpp
class Metrics : public IMetrics {
    // static 멤버 모두 제거 → 인스턴스 멤버로
    bool enabled_ = false;
    std::vector<std::unique_ptr<ActorMetrics>> actors_;
    std::vector<std::unique_ptr<WorkerMetrics>> workers_;
    DispatcherMetrics dispatcher_;
    
public:
    // IMetrics 인터페이스 구현
    // 생성자에서 numWorkers 받거나 init()에서 초기화
};
```

#### 1.5.2 `Log` → 인스턴스 기반
```cpp
// src/core/common/log/log.hpp
class Logger : public ILogger {
    std::atomic<LogLevel> level_{LogLevel::Info};
    std::mutex fileMutex_;
    FILE* logFile_ = nullptr;
    std::string appName_;
    thread_local static std::string threadBuffer_;  // 스레드 로컬 버퍼 유지
    
public:
    // ILogger 인터페이스 구현
    // thread_local 버퍼는 유지하되 flush 정책만 인스턴스 메서드로
};
```

#### 1.5.3 `Message` → `IAllocator` 주입
```cpp
// src/core/actor_system/messages/message.hpp
class Message {
    // MemoryPool::instance() 제거
    IAllocator* allocator_ = nullptr;  // 설정 시 주입
    
public:
    // 정적 팩토리 대신 빌더 패턴 또는 팩토리 객체 사용
    template<typename T>
    static Message make(T&& value, IAllocator* allocator) {
        Message msg;
        msg.allocator_ = allocator;
        // allocator_->allocate<T>(...) 사용
        return msg;
    }
    
    void setAllocator(IAllocator* alloc) { allocator_ = alloc; }
};

// 대안: MessageFactory 클래스 분리
class MessageFactory {
    IAllocator* allocator_;
public:
    explicit MessageFactory(IAllocator* alloc) : allocator_(alloc) {}
    template<typename T>
    Message make(T&& value) { /* allocator_ 사용 */ }
};
```

### 1.6 서비스 계층 경계 복원 — 포트 소유권 정리 (P0-7)

**문제**: `IPmu`/`ISys`/`II2c` 인터페이스가 `infra/hal/`에 있어 service가 infra에 의존. 또한 액터가 구현체를 `#if`로 직접 선택·생성해 하드웨어 결정이 비즈니스 로직에 침투.

**파일** (인터페이스만 이동 — 구현체는 infra 유지):
- `src/infra/hal/pmu/i_pmu.hpp` → `src/service/ports/i_pmu.hpp`
- `src/infra/hal/sys/i_sys.hpp` → `src/service/ports/i_sys.hpp`
- `src/infra/hal/i2c/i_i2c.hpp` → `src/service/ports/i_i2c.hpp`

**리팩토링**:
```cpp
// Before: cmd_actor.cpp — 액터가 구현체를 직접 선택/생성
#include "infra/hal/pmu/pmu_rsp5.hpp"
pmu_ = [](){
#if V2_PLATFORM_LINUX && defined(__aarch64__)
    return std::make_unique<PmuRsp5>();
#else
    return std::make_unique<PmuMock>();
#endif
}();

// After: 생성자 주입 — 구현 선택은 composition root(app)에서
class CmdActor : public Actor {
public:
    CmdActor(std::string name, uint64_t id, IPmu* pmu)
        : Actor(std::move(name), id), pmu_(pmu) {}
private:
    IPmu* pmu_;   // 소유하지 않음
};
```

- [ ] 3개 HAL 포트 `service/ports/`로 이동 + 모든 include 경로 수정
- [ ] `cmd_actor`, `monitor_actor`의 `#if` 구현 선택 제거 → 생성자 주입으로
- [ ] 플랫폼 분기 로직은 app composition root로 이동 (`PmuRsp5`/`SysLinux`를 여기서 생성·주입)
- [ ] `monitor_actor.cpp`의 `nlohmann_json` 직접 사용 제거 → 직렬화는 infra 어댑터로 위임
- [ ] `monitor_actor`의 UDS 직접 사용 제거 → `IIpcServer` 포트로 교체 (구현: `uds_server`)

### 1.7 서비스 전용 메시지 이관 (P0-8)

**문제**: `core/actor_system/messages/`에 서비스 전용 계약이 있어 core 엔진이 비즈니스 도메인(db/wifi/cmd/...)을 앎.

**원칙**: 메시지도 Port처럼 **소유 도메인(서비스) 폴더에 co-location**. core는 엔진 범용 타입만 유지.

**이관 대상**:
- `core/actor_system/messages/cmd_messages.hpp` → `service/cmd/cmd_messages.hpp`
- `.../ipc_messages.hpp` → `service/ipc/ipc_messages.hpp`
- `.../dbus_messages.hpp` → `service/dbus/dbus_messages.hpp`
- `.../monitor_messages.hpp` → `service/monitor/monitor_messages.hpp`
- `.../tick_messages.hpp` → `service/tick/tick_messages.hpp`
- `.../device_manager_messages.hpp` → `service/device_manager/device_manager_messages.hpp`
- `.../network_manager/network_manager_messages.hpp`, `.../network_manager/wifi_messages.hpp` → `service/network_manager/`

**core에 유지**: `message.hpp`(envelope), `message_traits.hpp`, `system_messages.hpp`(엔진 수명주기 — ActorEnable/Disable/Restart)

**참고**: Phase 3.4의 `V2_MESSAGE_TRAITS` 매크로로 **어떤 타입이든 메시지로 등록**되므로 core는 서비스 타입을 몰라도 됨. 서비스 간 공유 메시지는 "소유 서비스가 원천"이고 소비자는 해당 헤더만 include (데이터 계약이라 허용).

- [ ] 각 서비스 메시지 파일 이관 + 모든 `#include` 경로 수정
- [ ] 이관 후 `core/actor_system/messages/`에 서비스 타입이 안 남는지 검증 (CI 스캔)

### 1.8 서비스 간 결합 완화 (P0-9)

**문제**: `network_manager_actor.cpp`가 `service/dbus/dbus_actor.hpp`를 직접 include (구체 액터 타입 결합).

**원칙**: 서비스 간 통신은 메시지/레지스트리 조회로. 구체 액터 헤더 include 금지.

- [ ] `network_manager → dbus_actor` 직접 참조 제거 → 메시지 기반 통신으로 전환
- [ ] 서비스 디렉토리 간 `#include` 현황을 CI에서 스캔 (위반 시 실패)

---

## Phase 2: 인프라 구현체 이관 (2-3주) — **P1 High**

### 2.1 디렉토리 구조 재편
```
src/
├── core/                          # 순수 인터페이스 + 도메인 로직만 (Port는 도메인 co-location)
│   ├── actor_system/
│   │   ├── runtime/
│   │   │   ├── i_actor_runtime.hpp     # Port (co-location)
│   │   │   ├── i_scheduler.hpp         # Port (co-location)
│   │   │   ├── i_timer_service.hpp     # Port (co-location)
│   │   │   ├── actor_runtime.hpp       # 경량화된 런타임
│   │   │   ├── lifecycle_handler.hpp
│   │   │   └── dispatcher/
│   │   │       ├── i_work_dispatcher.hpp   # Port (co-location)
│   │   │       └── io/i_event_loop.hpp     # Port (co-location)
│   │   └── ...
│   ├── common/                       # std-only 순수 도메인 (포트 co-location)
│   │   ├── config/                   # platform_config.h, runtime_config.h (구조체만)
│   │   ├── container/                # cache_line, lock_free_mpsc_queue, ring_buffer
│   │   ├── time/                     # time.hpp/cpp, sleep.hpp
│   │   ├── timer/                    # i_timer.hpp, timer_base.hpp/cpp, timer.hpp/cpp (portable)
│   │   ├── memory/                   # i_memory_allocator.hpp, memory_pool, slab, size_class, chunk, free_list, thread_local_cache
│   │   ├── log/                      # i_logger.hpp, log.hpp/cpp (기본 구현)
│   │   └── util/                     # return.hpp, debug.hpp
│   └── perf/metrics/i_metrics.hpp     # IMetrics만 정의 (co-location)
│
├── infra/                         # 모든 구현체
│   ├── platform/
│   │   ├── linux/
│   │   │   ├── epoll_event_loop.hpp/cpp
│   │   │   ├── timer_linux.hpp/cpp          # LinuxTimer (timerfd, portable Timer 대체)
│   │   │   ├── posix_thread.hpp/cpp
│   │   │   ├── posix_logger.hpp/cpp
│   │   │   └── system_clock.hpp/cpp
│   │   ├── macos/
│   │   │   └── ...
│   │   └── windows/
│   │       └── ...
│   ├── memory/
│   │   ├── memory_pool_allocator.hpp/cpp    # IAllocator 구현
│   │   ├── slab_allocator.hpp/cpp
│   │   └── thread_local_cache.hpp
│   ├── logging/
│   │   ├── file_logger.hpp/cpp              # ILogger 구현
│   │   ├── console_logger.hpp/cpp
│   │   └── structured_logger.hpp/cpp
│   ├── metrics/
│   │   ├── metrics_collector.hpp/cpp        # IMetrics 구현
│   │   └── prometheus_exporter.hpp/cpp
│   ├── config/
│   │   ├── json_config_loader.hpp/cpp       # nlohmann_json 의존
│   │   └── runtime_config.hpp
│   └── ui/
│       ├── ftxui_renderer.hpp/cpp           # ftxui 의존
│       └── tui_widgets/
│
├── service/                         # Use Cases — 비즈니스 로직 (core + 자체 포트만)
│   ├── device_manager/
│   ├── network_manager/             # + wifi_messages.hpp
│   ├── monitor/
│   ├── dbus/                        # + i_dbus_handler.hpp (Port)
│   ├── ipc/                         # + i_ipc_server.hpp (Port)
│   ├── cmd/
│   ├── system/
│   ├── tick/
│   └── ports/                       # i_pmu.hpp, i_sys.hpp, i_i2c.hpp (소비자 소유 Port)
│
├── app/                             # Composition Root — 구체 타입 생성/주입
│   ├── cli/
│   ├── tui/
│   └── main/
```

### 2.2 플랫폼별 구현체 이관

#### 2.2.1 `infra/platform/linux/epoll_event_loop.hpp`
```cpp
// src/core/actor_system/runtime/dispatcher/io/event_loop_epoll.hpp → 이관
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"

class EpollEventLoop : public IEventLoop {
    // 기존 EventLoopEpoll 구현 거의 그대로
    // 단, Core 인터페이스만 사용
};
```

#### 2.2.2 `infra/platform/linux/timer_linux.hpp` ✅ (1.1에서 구현 완료)
```cpp
// core `common/timer/timer_base.hpp` 파생 — portable Timer(스레드+세마포어)의 Linux timerfd 최적화판
// (portable Timer는 core 유지 — 단독 빌드/실행 가능 원칙; Phase 2에서 ITimerService로 승격 시 함께 갱신)
#include "core/common/timer/i_timer.hpp"

class LinuxTimer : public TimerBase {
    // 기존 timer_fd.cpp의 Linux 브랜치 거의 그대로: timerfd_create, scheduleNextTimer(timerfd_settime), fd() 등
};
```

#### 2.2.3 `infra/platform/linux/posix_thread.hpp`
```cpp
// worker.cpp의 pthread_setname_np 등 추출
#include "core/actor_system/runtime/dispatcher/i_thread.hpp"

class PosixThread : public IThread {
    void setName(std::string_view name) override {
        pthread_setname_np(pthread_self(), name.data());
    }
    // ...
};
```

### 2.3 메모리 할당자 구현체

#### 2.3.1 `infra/memory/memory_pool_allocator.hpp`
```cpp
#include "core/common/memory/i_memory_allocator.hpp"
#include "core/common/memory/slab.hpp"
#include "core/common/memory/size_class.hpp"
#include "core/common/memory/thread_local_cache.hpp"

class MemoryPoolAllocator : public IMemoryAllocator {
    // 기존 MemoryPoolT 구현을 IAllocator로 래핑
    // 디버그 정책, 할당 정책 템플릿 파라미터로 외부화
};
```

### 2.4 로거 구현체

#### 2.4.1 `infra/logging/file_logger.hpp`
```cpp
#include "core/common/log/i_logger.hpp"

class FileLogger : public ILogger {
    std::mutex mutex_;
    FILE* file_ = nullptr;
    std::atomic<LogLevel> level_{LogLevel::Info};
    std::string appName_;
    thread_local static std::string buffer_;
    
    void log(LogLevel level, std::string_view file, int line, std::string_view func, std::string_view msg) override {
        if (level < level_.load()) return;
        // 기존 log.cpp 로직 거의 그대로
        // thread_local 버퍼 사용, 파일/콘솔 동시 출력
    }
    // ...
};
```

### 2.5 메트릭 수집기

#### 2.5.1 `infra/metrics/metrics_collector.hpp`
```cpp
#include "core/perf/metrics/i_metrics.hpp"

class MetricsCollector : public IMetrics {
    // 기존 Metrics 구현 거의 그대로
    // static → 인스턴스 멤버로 변경
};
```

### 2.6 설정 로더 (nlohmann_json 의존 격리)

#### 2.6.1 `infra/config/json_config_loader.hpp`
```cpp
#include "core/common/config/runtime_config.h"
#include <nlohmann/json.hpp>

class JsonConfigLoader {
public:
    static RuntimeConfig loadFromFile(const std::string& path) {
        std::ifstream f(path);
        nlohmann::json j = nlohmann::json::parse(f);
        // JSON → RuntimeConfig 매핑
        return config;
    }
};
```

---

## Phase 3: 성능 병목 해소 및 품질 향상 (2주) — **P1-P2**

### 3.1 Scheduler O(N) 정리 제거 (P1-1)

**파일**: `src/core/actor_system/runtime/scheduler.cpp` (또는 `infra/platform/linux/timer_linux.cpp`)

```cpp
// Before: 매 addTimer마다 전체 맵 순회
void Scheduler::cleanupTimerCtxs() {
    for (auto it = timerCtxs_.begin(); it != timerCtxs_.end(); ) {
        if (!timer_.isAlive(it->first)) it = timerCtxs_.erase(it);
        else ++it;
    }
}

// After: 타이머 만료 시 즉시 정리 (executeExpiredTimers에서)
void LinuxTimer::executeExpiredTimers() {
    // ... 기존 로직 ...
    // 만료된 타이머 처리 후 즉시 timerCtxs_에서 제거
    // 별도 cleanupTimerCtxs() 호출 불필요
}

// 또는: 지연 정리 플래그 사용
class LinuxTimer {
    std::atomic<bool> needsCleanup_{false};
    
    void addTimer(...) {
        // ...
        if (timerCtxs_.size() > CLEANUP_THRESHOLD) needsCleanup_.store(true);
    }
    
    void executeExpiredTimers() {
        // ...
        if (needsCleanup_.load()) {
            cleanupTimerCtxs();
            needsCleanup_.store(false);
        }
    }
};
```

### 3.2 ActorRegistry 락 분할 (P1-2)

**파일**: `src/core/actor_system/runtime/actor_registry.hpp/cpp`

```cpp
// Before: 단일 mutex_로 모든 연산 직렬화
mutable std::mutex mutex_;

// After: 읽기/쓰기 분리 (shared_mutex C++17)
#include <shared_mutex>

class ActorRegistry : public IActorRegistry {
    mutable std::shared_mutex mutex_;  // C++17
    // 또는 folly::RWSpinLock 등 고성능 RW락
    
    ActorHandle findByName(const std::string& name) {
        std::shared_lock lock(mutex_);  // 읽기: 공유 락
        // ...
    }
    
    void add(Actor* actor) {
        std::unique_lock lock(mutex_);  // 쓰기: 배타 락
        // ...
    }
    
    void forEachActor(const std::function<void(ActorHandle)>& callback) const {
        std::vector<ActorHandle> snapshot;
        {
            std::shared_lock lock(mutex_);
            snapshot.reserve(byId_.size());
            for (auto& [id, entry] : byId_) {
                snapshot.emplace_back(id, entry.generation, this);
            }
        }
        // 락 해제 후 콜백 실행 (콜백 내부에서 registry 수정 방지)
        for (const auto& handle : snapshot) callback(handle);
    }
};
```

### 3.3 Supervisor 메서드 분해 (P1-3)

**파일**: `src/core/actor_system/runtime/supervisor/supervisor.cpp`

```cpp
void Supervisor::onActorFailed(ISupervised* runtime, Message failedMsg, const std::string& reason) {
    if (!runtime) return;
    
    recordFailure(runtime);
    auto [strategy, limit] = snapshotPolicy(runtime->actorId());
    auto letter = createDeadLetter(runtime, reason, std::move(failedMsg));
    drainToDeadLetter(runtime, letter.timestampNs);
    applyStrategy(runtime, strategy, limit, reason);
}

// Private helper methods
void Supervisor::recordFailure(ISupervised* runtime) {
    totalFailures_.fetch_add(1, std::memory_order_relaxed);
    V2_LOG_ERROR("Actor {} crashed: {}", runtime->actorName(), reason);
}

std::pair<RestartStrategy, int> Supervisor::snapshotPolicy(uint64_t actorId) {
    std::lock_guard lock(mutex_);
    RestartStrategy strategy = defaultStrategy_;
    int limit = maxRestarts_;
    auto it = perActorStrategy_.find(actorId);
    if (it != perActorStrategy_.end()) strategy = it->second;
    return {strategy, limit};
}

DeadLetter Supervisor::createDeadLetter(ISupervised* runtime, const std::string& reason, Message failedMsg) {
    uint64_t nowNs = clock_ ? clock_->nowNs() : 0;  // IClock 주입 필요
    return {runtime->actorId(), runtime->actorName(), reason, nowNs, std::move(failedMsg)};
}

void Supervisor::drainToDeadLetter(ISupervised* runtime, uint64_t timestampNs) {
    Message msg;
    while (runtime->drainMailbox(msg)) {
        DeadLetter rest{runtime->actorId(), runtime->actorName(), "drained", timestampNs, std::move(msg)};
        if (!deadLetterQueue_.push(std::move(rest))) {
            V2_LOG_WARN("Dead letter queue full, dropping drained message from {}", runtime->actorName());
        }
    }
}

void Supervisor::applyStrategy(ISupervised* runtime, RestartStrategy strategy, int limit, const std::string& reason) {
    switch (strategy) {
        case RestartStrategy::OneForOne:
            applyOneForOne(runtime, limit, reason);
            break;
        case RestartStrategy::OneForAll:
            applyOneForAll(runtime, limit, reason);
            break;
        case RestartStrategy::None:
            applyNone(runtime, reason);
            break;
    }
}

void Supervisor::applyOneForOne(ISupervised* runtime, int limit, const std::string& reason) {
    if (runtime->tryRestart(reason, limit)) {
        totalRestarts_.fetch_add(1, std::memory_order_relaxed);
    } else {
        V2_LOG_ERROR("Actor {} exceeded max restarts ({}), shutting down", runtime->actorName(), limit);
        runtime->shutdown();
    }
}

void Supervisor::applyOneForAll(ISupervised* runtime, int limit, const std::string& reason) {
    std::function<int()> fn;
    bool underBudget = false;
    {
        std::lock_guard lock(mutex_);
        fn = restartAll_;
        int& n = oneForAllRestartCount_[runtime->actorId()];
        underBudget = (n < limit);
        if (underBudget) n++;
    }
    if (!underBudget) {
        V2_LOG_ERROR("Actor {} exceeded max one-for-all restarts ({}), shutting down", runtime->actorName(), limit);
        runtime->shutdown();
        return;
    }
    int restarted = 0;
    if (fn) {
        try { restarted = fn(); }
        catch (const std::exception& e) { V2_LOG_ERROR("restartAll callback threw: {}", e.what()); }
        catch (...) { V2_LOG_ERROR("restartAll callback threw unknown exception"); }
    }
    oneForAllBroadcasts_.fetch_add(1, std::memory_order_relaxed);
    if (restarted > 0) totalRestarts_.fetch_add(restarted, std::memory_order_relaxed);
}

void Supervisor::applyNone(ISupervised* runtime, const std::string& reason) {
    V2_LOG_WARN("Actor {} restart disabled by policy, shutting down", runtime->actorName());
    runtime->shutdown();
}
```

### 3.4 메시지 시스템 리팩토링 (P1-4) — **Shotgun Surgery 해결**

#### 3.4.1 타입 안전한 메시지 ID
```cpp
// src/core/actor_system/messages/message_id.hpp
#include <cstdint>
#include <type_traits>

class MessageId {
    uint32_t value_;
    constexpr explicit MessageId(uint32_t v) : value_(v) {}
public:
    constexpr MessageId() : value_(0) {}
    constexpr bool operator==(MessageId other) const { return value_ == other.value_; }
    constexpr bool operator!=(MessageId other) const { return value_ != other.value_; }
    constexpr explicit operator bool() const { return value_ != 0; }
    constexpr uint32_t raw() const { return value_; }
    
    // 시스템 예약 ID
    static constexpr MessageId SignalNotify{1};
    static constexpr MessageId Tick{2};
    // ...
    
    // 사용자 정의 ID 생성 (컴파일 타임)
    template<uint32_t V>
    static constexpr MessageId user() { return MessageId{V}; }
};

// 해시 지원
namespace std {
    template<> struct hash<MessageId> {
        size_t operator()(MessageId id) const noexcept { return id.raw(); }
    };
}
```

#### 3.4.2 메시지 트레이트 자동 등록 (매크로/템플릿)
```cpp
// src/core/actor_system/messages/message_traits.hpp
#define V2_MESSAGE_TRAITS(Type, IdValue) \
    namespace v2::message_traits { \
        template<> struct Traits<Type> { \
            static constexpr MessageId kId = MessageId::user<IdValue>(); \
            static constexpr bool kIsCopyable = std::is_copy_constructible_v<Type>; \
            static constexpr size_t kSize = sizeof(Type); \
            static constexpr size_t kAlign = alignof(Type); \
        }; \
    }

// 사용 예시
struct Tick {
    uint64_t sequence = 0;
};
V2_MESSAGE_TRAITS(Tick, 100)  // 한 곳에서 ID + 트레이트 동시 정의

struct CmdRequest {
    std::string command;
    std::vector<std::string> args;
};
V2_MESSAGE_TRAITS(CmdRequest, 101)
```

#### 3.4.3 Message 팩토리에서 트레이트 자동 활용
```cpp
// src/core/actor_system/messages/message.hpp
template<typename T>
static Message make(T&& value, IAllocator* allocator) {
    using DT = std::decay_t<T>;
    using Traits = v2::message_traits::Traits<DT>;
    
    Message msg;
    msg.id_ = Traits::kId;
    msg.allocator_ = allocator;
    // Traits::kSize, kAlign, kIsCopyable로 스토리지 모드 결정
    // ...
    return msg;
}
```

### 3.5벤치마크/메트릭 코어 선택적 링크 (P2-5)

**파일**: `src/core/core.cmake`

```cmake
# perf 모듈 별도 타겟으로 분리
add_library(v2_core_perf OBJECT)
target_sources(v2_core_perf PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/perf/metrics/metrics.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/benchmark.cpp
    ${CMAKE_CURRENT_LIST_DIR}/perf/benchmark/bench_*.cpp
)
target_link_libraries(v2_core_perf PUBLIC v2_core)

# 메인 Core 타겟에서는 perf 제외
# 필요한 앱/테스트에서 v2_core_perf 링크
```

---

## Phase 4: 도메인 모델 풍부화 및 마무리 (1-2주) — **P2**

### 4.1 Actor Anemic Model 개선 (P2-1)

**파일**: `src/core/actor_system/actor/actor.hpp/cpp`

```cpp
// Before: 모든 행위 ActorRuntime에 위임
class Actor {
    void sendMsg(const std::string& name, Message msg) { runtime_->actorRegistry()->findByName(name).send(std::move(msg)); }
    int startTimer(Message msg, uint64_t delayMs, bool repeating) { return runtime_ ? runtime_->addTimer(this, std::move(msg), delayMs, repeating) : Fail; }
    // ...
};

// After: Actor가 자신의 행위 캡슐화, 런타임은 인프라만 제공
class Actor {
protected:
    // 인프라 접근자 (protected로 파생 클래스만 사용)
    IActorRegistry* registry() const { return runtime_->actorRegistry(); }
    ITimerService* timers() const { return runtime_->timers(); }
    IWorkDispatcher* dispatcher() const { return runtime_->dispatcher(); }
    ISupervisor* supervisor() const { return runtime_->supervisor(); }
    IMailbox* mailbox() const { return runtime_->mailbox(); }
    IMetrics* metrics() const { return runtime_->metrics(); }
    ILogger* logger() const { return runtime_->logger(); }
    IClock* clock() const { return runtime_->clock(); }

public:
    // 행위 메서드들: 인프라 인터페이스 통해 구현
    void sendMsg(const std::string& name, Message msg) {
        ActorHandle target = registry()->findByName(name);
        if (target.valid()) target.send(std::move(msg));
    }
    
    void sendMsg(uint64_t id, Message msg) {
        ActorHandle target = registry()->findById(id);
        if (target.valid()) target.send(std::move(msg));
    }
    
    int startTimer(Message msg, uint64_t delayMs, bool repeating) {
        return timers() ? timers()->addTimer(this, std::move(msg), delayMs, repeating) : Fail;
    }
    
    void receiveMsg(Message msg) { mailbox()->push(std::move(msg)); }
    // ...
};

// ActorRuntime은 순수 인프라 어댑터 역할만
class ActorRuntime : public IActorRuntime {
    // Actor에게 인터페이스 제공
    IActorRegistry* actorRegistry() const override { return registry_; }
    ITimerService* timers() const { return timerService_; }
    IWorkDispatcher* dispatcher() const { return workDispatcher_; }
    ISupervisor* supervisor() const { return supervisor_; }
    IMailbox* mailbox() const { return mailbox_.get(); }
    IMetrics* metrics() const { return metrics_; }
    ILogger* logger() const { return logger_; }
    IClock* clock() const { return clock_; }
    // ...
};
```

### 4.2 타입 안전한 설정 객체 (P2-2)

**파일**: `src/core/common/config/`

```cpp
// worker_config.hpp
struct WorkerConfig {
    int count = 1;
    int maxBatch = 32;
    std::string namePrefix = "v2-worker";
    
    static WorkerConfig fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

// mailbox_config.hpp
struct MailboxConfig {
    size_t capacity = 512;
    bool blockOnFull = false;  // 향후 확장
    
    static MailboxConfig fromJson(const nlohmann::json& j);
};

// epoll_config.hpp
struct EpollConfig {
    int maxEvents = 64;
    int waitTimeoutMs = 1000;
    
    static EpollConfig fromJson(const nlohmann::json& j);
};

// runtime_config.hpp (통합)
struct RuntimeConfig {
    WorkerConfig workers;
    MailboxConfig mailbox;
    EpollConfig epoll;
    LogConfig log;
    MetricsConfig metrics;
    // ...
    
    static RuntimeConfig loadFromFile(const std::string& path);  // infra/config에서 구현
};
```

### 4.3 IMailbox 인터페이스화 (P2-3)

**파일**: `src/core/actor_system/runtime/i_mailbox.hpp` (이미 Phase 1.2.5에서 정의 — Port는 actor_system 도메인에 co-location)

```cpp
// core/actor_system/runtime/i_mailbox.hpp (Port — actor_system 도메인 co-location)
class IMailbox {
public:
    virtual ~IMailbox() = default;
    virtual bool push(Message&& msg) = 0;
    virtual bool pop(Message& out) = 0;
    virtual size_t count() const = 0;
    virtual size_t capacity() const = 0;
    virtual bool empty() const = 0;
    virtual void clear() = 0;
};

// core/common/container/lock_free_mpsc_queue.hpp (구현체 — std-only 순수 구현이라 core 유지)
class LockFreeMpscQueue : public IMailbox {
    // 기존 구현 거의 그대로 (infra로 이동하지 않음 — OS/3rd-party 의존 없음)
};
```

### 4.4 RingBuffer 일반화 (P2-4)

```cpp
// src/core/common/container/ring_buffer.hpp — 인터페이스/구현 모두 core co-location
// (순수 std 구현이므로 infra로 이동하지 않음)
class IByteBuffer {
public:
    virtual ~IByteBuffer() = default;
    virtual int push(const uint8_t* data, size_t size) = 0;
    virtual int pop(uint8_t* out, size_t size) = 0;
    virtual void reset() = 0;
    virtual size_t count() const = 0;
    virtual size_t capacity() const = 0;
    virtual size_t freeSpace() const = 0;
    virtual bool empty() const = 0;
    virtual bool full() const = 0;
};

// 같은 폴더에 구현체 유지
class RingBuffer : public IByteBuffer { /* 기존 구현 */ };
```

### 4.5 최종 검증 및 문서화

- [ ] **전체 테스트 스위트 실행** — 단위/통합 테스트 모두 통과
- [ ] **클린 아키텍처 검증**:
  - [ ] `core/` 디렉토리에 `.cpp` 중 OS/라이브러리 의존 코드 없음 확인
  - [ ] `core/` 타겟 링크에 `ftxui`, `nlohmann_json`, `pthread` 등 없음 확인
  - [ ] 모든 구현체가 `infra/` 하위에 위치 확인
- [ ] **성능 벤치마크 실행** — 리팩토링 전/후 비교 (latency, throughput, contention)
- [ ] **API 문서 생성** (Doxygen) — 인터페이스 위주로 문서화
- [ ] **마이그레이션 가이드 작성** — 기존 코드 변경 사항 정리

---

## 마일스톤 및 체크포인트

| 마일스톤 | 목표일 | 검증 기준 |
|----------|--------|-----------|
| **M1: 인터페이스 완성** | Week 1 말 | 모든 핵심 인터페이스 정의 완료, 컴파일 통과 |
| **M2: DI 컨테이너 + ActorSystem 주입** | Week 2 말 | `ActorSystem` 생성자 주입 동작, 기존 테스트 통과 |
| **M3: ActorRuntime 분해** | Week 3 말 | `ActorRuntime` < 100줄, 책임 분리 확인 |
| **M4: 전역 상태 제거** | Week 4 말 | `Metrics`, `Log`, `MemoryPool` static 멤버 0개 |
| **M5: 인프라 이관 완료** | Week 6 말 | `core/`에 OS 의존 코드 0개, `infra/` 빌드 성공 |
| **M5b: 서비스 계층 경계 복원** | Week 6 말 | `service/ports/` 포트 이관, 서비스 전용 메시지 이관, service↔infra 직접 include 0개 |
| **M6: 성능 병목 해소** | Week 7 말 | Scheduler O(1), Registry 락 분할, 벤치마크 개선 |
| **M7: 도메인 모델 개선** | Week 8 말 | Actor Anemic Model 해소, 설정 타입 안전화 |
| **M8: 최종 릴리스** | Week 9-10 | 전체 테스트 통과, 문서화 완료, 성능 회귀 없음 |

---

## 위험 요소 및 대응

| 위험 | 확률 | 영향 | 대응 방안 |
|------|------|------|-----------|
| 인터페이스 설계 변경 시 하위 구현체 대량 수정 | 높음 | 높음 | Phase 1에서 인터페이스 **충분히 검토 후 동결**, 변경 시 ADR 문서화 |
| 테스트 코드 대량 수정 필요 | 높음 | 중간 | 테스트도 DI 컨테이너 활용, mock 주입 헬퍼 제공 |
| 성능 회귀 (인터페이스 오버헤드) | 낮음 | 높음 | `final` 클래스, 인라인 가상 함수, 템플릿 정책으로 가상 호출 최소화 |
| 서드파티 라이브러리 의존성 누락 | 중간 | 중간 | `infra/` CMake에서 모든 외부 의존 명시적 관리 |
| 팀 온보딩 지연 | 중간 | 낮음 | 아키텍처 문서 + 예제 코드 + 마이그레이션 가이드 제공 |

---

## 성공 지표 (KPI)

| 지표 | 현재 | 목표 | 측정 방법 |
|------|------|------|-----------|
| **Layer Separation 점수** | 2/10 | 9/10 | `core/`, `service/` 내 infra/3rd-party 심볼 참조 0개 |
| **Testability 점수** | 2/10 | 8/10 | 단위 테스트 Mock 없이 실행 가능, 병렬 테스트 지원 |
| **Maintainability 점수** | 3/10 | 7/10 | 순환 복잡도 평균 < 10, 클래스당 메서드 < 15 |
| **SOLID 준수율** | 30% | 85% | 정적 분석 도구 (cppcheck, clang-tidy) |
| **빌드 시간** | 기준선 | -20% | 모듈 분리 효과 |
| **벤치마크 지연시간** | 기준선 | ≤ 105% | 리팩토링 오버헤드 최소화 |
| **코드 커버리지** | 미측정 | > 80% | gcov/llvm-cov |

---

## 부록: 파일별 마이그레이션 매핑

| 현재 파일 | 대상 위치 | 비고 |
|-----------|-----------|------|
| `core/actor_system/runtime/dispatcher/io/event_loop_epoll.hpp/cpp` | `infra/platform/linux/epoll_event_loop.hpp/cpp` | `IEventLoop` 구현 |
| `infra/platform/linux/timer_fd.hpp/cpp` | `infra/platform/linux/timer_linux.hpp/cpp` | `LinuxTimer : TimerBase` (timerfd) — portable `Timer`는 core `common/time` 유지 |
| `core/common/os/epoll.hpp/cpp` | `infra/platform/linux/epoll.hpp/cpp` | 내부 구현 |
| `core/common/os/signal_handler.hpp/cpp` | `infra/platform/linux/signal_handler.hpp/cpp` | 내부 구현 |
| `core/common/log/log.hpp/cpp` | `core/common/log/i_logger.hpp` + `infra/logging/file_logger.hpp/cpp` | 인터페이스/구현 분리 |
| `core/common/config/runtime_config.cpp` | `infra/config/json_config_loader.hpp/cpp` | nlohmann 파싱 이관 (core는 구조체만) |
| `core/common/config/runtime_config.h` | `core/common/config/runtime_config.h` | 유지 — 필드명 `epoll*` → `eventLoop*` 일반화 |
| `core/common/util/sleep.hpp` | `core/common/time/sleep.hpp` | 시간 관련 유틸 이관 |
| `core/common/container/ring_buffer.hpp/cpp` | `core/common/container/ring_buffer.hpp/cpp` | 유지 — `IByteBuffer` 분리만 (구현은 core) |
| `core/perf/metrics/metrics.hpp/cpp` | `core/perf/metrics/i_metrics.hpp` + `infra/metrics/metrics_collector.hpp/cpp` | 인터페이스/구현 분리 |
| `core/common/memory/memory_pool.hpp` | `core/common/memory/i_memory_allocator.hpp` + `infra/memory/memory_pool_allocator.hpp/cpp` | 인터페이스/구현 분리 |
| `core/actor_system/messages/message.hpp` | `core/actor_system/messages/message.hpp` (인터페이스 유지) + `infra/messaging/message_factory.hpp` | 팩토리 분리 |
| `core/actor_system/messages/cmd_messages.hpp` | `service/cmd/cmd_messages.hpp` | 서비스 전용 메시지 이관 |
| `core/actor_system/messages/ipc_messages.hpp` | `service/ipc/ipc_messages.hpp` | 서비스 전용 메시지 이관 |
| `core/actor_system/messages/dbus_messages.hpp` | `service/dbus/dbus_messages.hpp` | 서비스 전용 메시지 이관 |
| `core/actor_system/messages/monitor_messages.hpp` | `service/monitor/monitor_messages.hpp` | 서비스 전용 메시지 이관 |
| `core/actor_system/messages/tick_messages.hpp` | `service/tick/tick_messages.hpp` | 서비스 전용 메시지 이관 |
| `core/actor_system/messages/device_manager_messages.hpp` | `service/device_manager/device_manager_messages.hpp` | 서비스 전용 메시지 이관 |
| `core/actor_system/messages/network_manager/*.hpp` | `service/network_manager/*.hpp` | 서비스 전용 메시지 이관 |
| `infra/hal/pmu/i_pmu.hpp` | `service/ports/i_pmu.hpp` | 포트 소유권 이전 (소비자 소유) |
| `infra/hal/sys/i_sys.hpp` | `service/ports/i_sys.hpp` | 포트 소유권 이전 (소비자 소유) |
| `infra/hal/i2c/i_i2c.hpp` | `service/ports/i_i2c.hpp` | 포트 소유권 이전 (소비자 소유) |
| `infra/hal/pmu/pmu_rsp5.hpp/cpp` | `infra/hal/pmu/pmu_rsp5.hpp/cpp` | 유지 — `IPmu` 구현체 |
| `infra/hal/sys/sys_linux.hpp/cpp` | `infra/hal/sys/sys_linux.hpp/cpp` | 유지 — `ISys` 구현체 |
| `infra/hal/i2c/i2c_linux.hpp/cpp` | `infra/hal/i2c/i2c_linux.hpp/cpp` | 유지 — `II2c` 구현체 |
| `infra/transport/uds/uds_server.hpp/cpp` | `infra/transport/uds/uds_server.hpp/cpp` | 유지 — `IIpcServer` 구현 (포트는 `service/ipc/`) |
| `core/core.cmake` | `core/core.cmake` (인터페이스만) + `infra/infra.cmake` (구현체) | CMake 분리 |
| `service/network_manager/network_manager_actor.cpp` | (수정) `dbus_actor.hpp` 직접 include 제거 | 메시지 기반 통신으로 전환 |

---

*문서 버전: 1.0*
*작성일: 2026-08-01*
*검토자: Software Architect*