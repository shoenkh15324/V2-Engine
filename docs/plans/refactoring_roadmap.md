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
> - **빌드 시스템이 경계를 강제**: CMake 타깃 의존성으로 계층 위반을 컴파일/링크 단계에서 검출. 의존 방향은 **안쪽으로만**: `v2_app → v2_infra/v2_service → v2_core` (각 레이어는 자신보다 안쪽 레이어만 링크, **`v2_core`는 아무것도 링크하지 않음**). 단, **service 소유 포트를 구현하는 infra는 해당 포트 헤더를 include** (header-only, 링크 아님 → infra→service 헤더 의존, 1.6). core를 독립 subproject로 분리해 단독 빌드/실행을 CI에서 증명 (1.1)

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
│   │   └── log.hpp/cpp                    # LOGGER (1.5.2) — 인스턴스 + activeLogger, LogLevel 소속 (i_logger.hpp 제거)
│   └── util/                              # 공용 유틸 (std-only)
│       ├── return.hpp                     # 유지 — Ok/Fail + Result<T>
│       └── debug.hpp                      # 유지 (1.5.2) — V2_ASSERT/V2_PANIC, 로그는 logBlock 경유
└── perf/metrics/                          # [Entities] Metrics → 인스턴스 기반 (Phase 1.5)

src/infra/                                 # 외부 원: Adapters — OS/외부 라이브러리 의존만
├── platform/linux/
│   ├── event_loop_epoll.hpp/cpp           # MOVE: dispatcher/io/ (IEventLoop 구현)
│   ├── timer_linux.hpp/cpp                # NEW (1.1) — LinuxTimer (timerfd, core TimerBase 파생) — portable 대체 최적화
│   └── signal_handler.hpp/cpp             # MOVE: common/os/
├── threading/
│   └── posix_thread.hpp/cpp               # worker의 pthread_setname_np 분리
├── memory/                                # 특수/외부 할당자만 (기본 풀은 core 소유 — 1.2.2)
│   └── (linux_arena.hpp/cpp 등)           # 플랫폼/외부 라이브러리 의존 전략만
├── config/json_config_loader.hpp/cpp      # nlohmann_json 의존
├── ui/ftxui_renderer.hpp/cpp              # ftxui 의존
└── mock/                                  # MockAllocator, TestRegistry

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

> **현재 상태 (1.1 완료)**: `v2_core`는 std(+libc)만 사용하고 infra/`nlohmann`/`ftxui` 참조 0건. `v2_core`는 `Threads::Threads`만 링크하는 **독립 CMake subproject**로 분리됨. core→infra 링크 제거, compile definitions는 app.cmake로 이관 완료. 경계는 `v2_core_smoke`(v2_core만 링크) + 순수 테스트 링크 라인 검증으로 강제됨.
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
# smoke 타깃은 test/standalone/ 에 등록됨 (v2_core만 링크 — 경계 증명)
```

- [x] `src/core/CMakeLists.txt` 신규 → root에서 `add_subdirectory(src/core)` 직접 호출 (전환 완료 — `core.cmake` 위임 스텁은 삭제) ✅
- [x] `test/standalone/main.cpp` smoke 타깃 — 모든 core 오브젝트 + trivial main을 링크해 "외부 심볼 무참조" 증명 (매 빌드 경계 강제). core portable `Timer` + std-only mock/블로킹 루프를 주입해 **메시징 + 타이머를 실제 실행** (fd 기능은 데모 범위 밖) ✅ (`test/standalone/mock_event_loop.hpp` + `main.cpp` + `standalone_test.cmake` — **v2_core만 링크**, `cmake -S src/core` 단독 빌드는 OBJECT lib 컴파일 독립성만 증명)
- [x] `v2_core`의 compile definitions(`V2_ENGINE_NAME/VERSION/V2_CONFIG_DIR`) → `app.cmake`로 이관 (전부 app에서만 사용됨) ✅
- [x] `V2_CONFIG_DIR` 경로 버그 수정: 현재 `src/core/config`(존재하지 않음 → config 미로딩) → `${CMAKE_SOURCE_DIR}/config` ✅
- [x] 의존 방향 교정: `v2_infra`에 `target_link_libraries(v2_infra PUBLIC v2_core)` 추가 **및 `v2_core`의 잔재 링크 `v2_infra` 제거** (core→infra 링크 제거) ✅
- [x] infra 의존 테스트(`test_event_loop_epoll`, `test_actor_system`, `test_actor_system_integration`, `test_timer_pipeline`)에 `v2_infra` 명시 ✅ (`test_timer`/`test_scheduler`는 portable만 사용이라 제외. `v2_bench`는 이미 `v2_infra` 링크 완료 — bench.cmake)

#### 1.1.3 검증
- [x] `rg 'infra/|nlohmann/|ftxui/' src/core` → 결과 없음 ✅
- [x] standalone 빌드: `cmake -S src/core -B <dir> && cmake --build <dir>` 성공 (OBJECT lib 컴파일 독립성) ✅
- [x] `v2_core_smoke` 빌드 + 실행 (`handled >= 3`, **v2_core만 링크** → 외부 심볼 무참조 증명) — root 빌드 `BUILD_TESTING=ON` + `ctest`로 검증 ✅ (Test #124)
- [x] 전체 빌드 + `ctest` 전체 통과 (124/124) ✅
- [x] 순수 테스트(`test_ring_buffer`) 링크 라인에 `libv2_infra.a` 미포함 ✅ (build.ninja에서 v2_core 객체만 링크 확인)

### 1.2 핵심 인터페이스 정의 (P0-2)

#### 1.2.1 `core/common/time/i_time_source.hpp` ~~— 삭제됨 (1.1에서 정리)~~ ✅
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

#### 1.2.2 `core/common/memory/i_memory_allocator.hpp` ✅

> **소유권 결정 (1.2 설계 확정)**: 할당자는 "포트 + 기본 구현"을 **core가 소유**, "특수/외부 구현"만 infra가 소유한다 (1.1 타이머 패턴과 동일: core portable `Timer` vs infra `LinuxTimer`).
> - **포트** `IMemoryAllocator` — core (재사용 계약)
> - **기본 구현** `MemoryPoolT`(현재 slab/TLS 풀) — **core 유지** (`::operator new`, `std::array`, `thread_local`, `cstring`만 사용 → std-only + portable 판정). core만으로 만든 다른 프로젝트가 재구현 없이 재사용 가능해야 함
> - **특수 구현**(Linux arena/hugepage, jemalloc/tcmalloc wrapper 등) — infra만. 2.3에서 확정
> - **파편화 정리**: 슬랩 설계(고정 SizeClass 블록 균일 재사용)는 파편화 방지 장치이므로 풀을 "하나로 강제"할 이유가 없음. 생산 기본은 공용 풀 인스턴스 1개, 테스트/서브시스템은 별도 인스턴스 생성 가능하게 (인스턴스 기반)
> - **실제 제거 대상은 "위치"가 아니라 "전역 싱글톤"** (`MemoryPool::instance()` + `inline thread_local tlCache_`) — `instance()`는 1.5.3에서 제거 ✅, `tlCache_`는 **후속(B)으로 연기** (1.5.3 판정 참고: 비정적 멤버 `thread_local` 불가 → 레지스트리 재설계 필요, 2.3 arena와 함께)

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

#### 1.2.3 `core/common/log/i_logger.hpp` ✅
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

#### 1.2.4 `core/perf/metrics/i_metrics.hpp` ✅
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

#### 1.2.5 `core/actor_system/runtime/i_mailbox.hpp` ✅
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

#### 1.2.5 나머지 인터페이스들도 동일 패턴으로 정의 ✅
- `i_event_loop.hpp` — `subscribe`, `unsubscribe`, `run`, `stop`, `post`
- `i_work_dispatcher.hpp` — `dispatch`, `redispatch`, `acquire`, `start`, `stop`, `drainAndStop`, `beginDrain`, `isDraining`, `pendingWork`, `onWorkDone`
- `i_scheduler.hpp` — `ITimerService`와 통합 또는 별도
- `i_actor_registry.hpp` — 기존 `IActorRegistry` 유지
- `i_supervisor.hpp` — 기존 `ISupervisor` 유지
- `i_dead_letter_queue.hpp` — `push`, `pop`, `count`, `capacity`

### 1.3 ActorSystem 생성자 주입 리팩토링 (P0-3) ✅

> **목적 (이전 설계 대체)**: ActorSystem의 **공개 계약(헤더)에서 구체 타입 제로**. 모든 협력자는 생성자로 주입받고, **기본 조립은 클래스 밖 팩토리(Composition Root)가 담당**한다.
>
> **이전 설계에서 제거/수정한 지점**:
> - `IActorRegistry&`(비소유 참조) → **`unique_ptr<IActorRegistry>` 소유** (소유권 모호성 해소, 전 의존성 일관)
> - `IClock* clock_` → **제거** (`IClock` 타입은 1.1에서 `i_time_source.hpp` 삭제로 미존재)
> - `IMetrics*`/`ILogger*` → **보류** (인스턴스 전환은 1.5.1/1.5.2의 몫 — 지금 추가는 데드 파라미터)
> - `epollMaxEvents`/`epollWaitTimeoutMs` → **제거** (infra 전용이며 이벤트루프는 주입 전 이미 호출자가 구성)
> - `createDefault()`가 core에서 이벤트루프를 못 만드는 문제 → **eventLoop는 팩토리 파라미터로 수신**

**파일**: `src/core/actor_system/actor_system.hpp`, `actor_system.cpp`, `i_supervisor.hpp`

```cpp
// Before: 내부에서 모든 구현체 생성 (WorkDispatcher/Scheduler/Registry/DLQ/Supervisor)
explicit ActorSystem(int numWorkers, int maxBatch = 32,
                     std::unique_ptr<IEventLoop> eventLoop = nullptr,
                     std::unique_ptr<ITimer> timer = nullptr);

// After: 인터페이스만 받는 순수 계약 + 번들 struct (fat ctor 방지, 전부 소유)
struct ActorSystemConfig {
    int numWorkers = 1;
    int maxBatch = 32;
    size_t defaultMailboxSize = 512;   // epoll 필드 없음 (infra 전용)
};

struct ActorSystemDeps {
    std::unique_ptr<IWorkDispatcher>   dispatcher;
    std::unique_ptr<IEventLoop>        eventLoop;      // run()에 필수 — core가 못 만듦
    std::unique_ptr<IScheduler>        scheduler;
    std::unique_ptr<IDeadLetterQueue>  deadLetterQueue;
    std::unique_ptr<ISupervisor>       supervisor;
    std::unique_ptr<IActorRegistry>    registry;       // 소유 (참조 아님)
};

class ActorSystem {
    // 멤버 전부 unique_ptr<인터페이스> — 헤더에 구체 include 0개
public:
    ActorSystem(const ActorSystemConfig& config, ActorSystemDeps deps);
    // ...
};
```

```cpp
// 기본 조립 (클래스 밖 — core/actor_system.cpp의 자유 함수)
std::unique_ptr<ActorSystem> createDefaultActorSystem(
    const ActorSystemConfig& config,
    std::unique_ptr<IEventLoop> eventLoop,          // core가 못 만드는 유일한 것
    std::unique_ptr<ITimer> timer = nullptr);       // → Scheduler 내부 조립
// 팩토리가 WorkDispatcher/Scheduler(timer)/DeadLetterQueue/Supervisor(*dlq)/ActorRegistry를
// 만들고 dlq↔supervisor를 배선한 뒤 DI ctor 호출. ActorSystem 클래스 자체는 구체를 모름.
```

- [ ] `ActorSystemConfig` + `ActorSystemDeps` struct 추가 (매직 넘버 외부화, epoll 필드 제외)
- [ ] 멤버를 `unique_ptr<인터페이스>`로 교체, 헤더 include를 인터페이스만으로 정리 (구체 include 0)
- [ ] `createDefaultActorSystem(config, eventLoop, timer)` 팩토리 분리 — 기본 구현체 조립 + dlq↔supervisor 배선
- [ ] `i_supervisor.hpp`에 `setRestartAll(std::function<int()>)` 추가 (OneForAll 브로드캐스트는 ActorSystem이 런타임 집합을 아므로 내부 배선 필수 — 구체에 이미 존재, 인터페이스 승격만)
- [ ] `start()/stop()/run()`에 `eventLoop_` null 가드 (루프 없이 생성 가능 — 테스트/조립 편의)
- [ ] 기존 `(numWorkers, maxBatch, eventLoop, timer)` ctor 제거 → app/bench(~12곳)는 `createDefaultActorSystem`으로 치환
- [ ] 단위 테스트(`test_actor_system` 등)를 `ActorSystemDeps` + mock(`TestScheduler`/`TestRegistry`/`MockEventLoop`/mock dispatcher) 주입으로 전환 → **infra 의존 없는 단위 테스트 달성**

### 1.4 ActorRuntime 분해 (P0-4)

**파일**: `src/core/actor_system/runtime/actor_runtime/actor_runtime.hpp/cpp` → 분해

#### 1.4.0 현재 상태 (1.1–1.3 반영)

지난 작업(1.1–1.3)으로 `ActorRuntime`은 인터페이스 주입을 이미 상당 부분 달성했다. 하지만 **`run()`이 순수 메시지 루프와 부수효과(측정·메트릭·재디스패치)를 한 메서드에 섞고** 있고, 몇 가지 잔여 결합이 남는다:

- **생성자 (1.4.2 완료 반영)**: `(unique_ptr<Actor>, unique_ptr<IMailbox>, IWorkDispatcher*, IScheduler*, IActorRegistry*, IEventLoop*, ISupervisor*)`
  - `actor`만 소유·구체 타입, `mailbox`는 `unique_ptr<IMailbox>` 인터페이스 소유 (구체 큐는 `Mailbox` 어댑터 내부에만 — **1.4.2에서 해소**)
- **IClock 없음**: 1.1에서 `i_time_source.hpp` 제거됨 → 시간 측정은 core `Time`(static 유틸) 직접 사용 (`Time::now()`, `Time::toNs()`)
- **타이머는 IScheduler 위임**: 별도 `ITimerService` 없음. `addTimer/cancelTimer/cancelAllTimers/timerCount`가 `scheduler_`로 위임
- **Metrics/Log는 전역 정적 사용**: `Metrics::isEnabled()/recordHandle()`, `V2_LOG_*` 매크로 (static singleton). 인스턴스(멤버) 전환은 **1.5.1/1.5.2의 몫** — 1.4에서 `IMetrics*`/`ILogger*` 멤버 추가는 하지 않음 (데드 파라미터 회피)
- **라이프사이클은 private 메서드로 인라인**: `tryConsumeLifecycle()`/`performRestart()`가 `ActorRuntime` 내부에 있음 (별도 `ILifecycleHandler` 없음 — 1.4.4 결정 사항)

#### 1.4.1 대상 클래스 구조 (현재 코드 기준)
```
ActorRuntime (메시지 루프 + 라이프사이클 + 타이머 위임)
├── unique_ptr<Actor> actor_
├── unique_ptr<IMailbox> mailbox_                     // 1.4.2 완료 — 구체 큐는 Mailbox 어댑터 내부
├── IWorkDispatcher* workDispatcher_                  // 재디스패치만
├── IScheduler* scheduler_                            // 타이머 위임 (addTimer/cancel/...)
├── IActorRegistry* actorRegistry_                    // 등록·해제만
├── IEventLoop* eventLoop_                            // 이벤트 등록만
├── ISupervisor* supervisor_                          // 실패 알림만
├── (타이머 추적: timerIds_/timerMutex_, 재시작: restartCount_, 정지: stopped_)
└── (Metrics/Log는 1.5 전까지 전역 static — 멤버 미추가)
```

```cpp
// 현재 생성자 시그니처 (1.4.2 완료 — IMailbox 인터페이스 주입)
ActorRuntime(std::unique_ptr<Actor> actor,
             std::unique_ptr<IMailbox> mailbox,
             IWorkDispatcher* workDispatcher, IScheduler* scheduler,
             IActorRegistry* actorRegistry, IEventLoop* eventLoop = nullptr,
             ISupervisor* supervisor = nullptr);
```

#### 1.4.2 mailbox → `IMailbox` 주입 (구체 타입 결합 제거) ✅
| 항목 | 변경 내용 |
|---|---|
| 어댑터 | `runtime/mailbox/mailbox.hpp` — `Mailbox : IMailbox` 신설 (`LockFreeMpscQueue<Message>` 래핑, `clear()`은 drain 루프) ✅ |
| 생성자 | `unique_ptr<LockFreeMpscQueue<Message>>` → `unique_ptr<IMailbox>` (header에서 구체 큐 include 제거) ✅ |
| 생성 측 | `ActorSystem::attachActor` + 단위 테스트 5개 → `std::make_unique<Mailbox>(N)` 전환 ✅ |
| 소유권 | `Mailbox`(구체) 생성은 조립 측, `ActorRuntime`은 `IMailbox`로 소유 ✅ |

> **경계점 (근거)**: `LockFreeMpscQueue`는 core `container/`의 std-only 순수 구현이라 위치상 경계 위반은 아니지만, `ActorRuntime` 헤더가 구체 큐를 include하면 포트 체계와 안 맞아 **타입 결합**을 제거했다. 구체 큐는 이제 어댑터(구현)에만 존재하고 선택은 조립 측이 한다.

#### 1.4.3 `run()` 순수화 (메시지 루프 ↔ 부수효과 분리) ✅
`run()`이 메시지 루프·라이프사이클·예외·측정·메트릭·재디스패치를 한 메서드에 섞고 있던 것을, **순수 메시지 처리(`processBatch`)와 부수효과 래퍼(`run`)** 로 나눈다. 시간 측정은 core `Time`(static)을 유지하고, 메트릭/로깅은 1.5 전까지 전역 `Metrics`/`V2_LOG`를 그대로 쓴다.

```cpp
// After (구현 완료): 순수 메시지 처리·라이프사이클·예외만
struct BatchResult { int processed = 0; bool hasMoreWork = false; };   // ActorRuntime private 중첩

ActorRuntime::BatchResult ActorRuntime::processBatch(int maxBatch){
    if(stopped_.load(std::memory_order_relaxed)) return {};
    Message msg;
    int processed = 0;
    while((maxBatch < 0) || (processed < maxBatch)){
        if(!mailbox_->pop(msg)) break;
        if(!tryConsumeLifecycle(msg)){          // 라이프사이클 메시지면 소비 (handle() 호출 안 함)
            try{ actor_->handle(msg); }
            catch(const std::exception& e){
                if(supervisor_){ supervisor_->onActorFailed(this, std::move(msg), e.what()); }
                else{ V2_LOG_ERROR("Actor {} threw: {}", actor_->name().c_str(), e.what()); }
                break;                          // ⚠️ 예외 시에도 래퍼에서 측정·메트릭·재디스패치 수행 (원본과 동일)
            }
            catch(...){
                if(supervisor_){ supervisor_->onActorFailed(this, std::move(msg), "unknown exception"); }
                else{ V2_LOG_ERROR("Actor {} threw unknown exception", actor_->name().c_str()); }
                break;
            }
        }
        processed++;
    }
    return { processed, !mailbox_->empty() };   // hasMoreWork = 큐에 잔량 존재 여부
}

// 래퍼: 측정·메트릭·재디스패치 (부수효과)
int ActorRuntime::run(int maxBatch, bool* hasMoreWork){
    if(hasMoreWork) *hasMoreWork = false;
    auto startTime = Time::now();
    auto r = processBatch(maxBatch);
    uint64_t gapNs = Time::toNs(Time::now() - startTime);
    if(Metrics::isEnabled()) Metrics::recordHandle(actor_->id(), r.processed, gapNs);
    if(r.hasMoreWork && workDispatcher_){
        if(hasMoreWork) *hasMoreWork = workDispatcher_->redispatch(this);
    }
    return r.processed;
}
```

> **설계 메모**
> - 네이밍: `runPure`/`RunResult` → **`processBatch`/`BatchResult`** — "최대 `maxBatch`개 큐 소모 처리"라는 임무를 이름에 반영. 공개 `run`은 `processBatch` + 계측/스케줄링 조합.
> - **예외 `break` vs `return`**: 실패 메시지는 `processed++`되지 않고, 이후 래퍼에서 여전히 측정·메트릭·재디스패치를 수행 (원본 `run`의 동작 1:1 보존). 초안의 `return`은 동작이 달라지므로 폐기.
> - **`stopped_` 차이 (의도적)**: 원본은 `stopped_`에서 메트릭 기록 없이 즉시 `return 0`했지만, 새 코드는 `run()`이 0배치로 `batches+1`을 기록. `stopped_`는 `shutdown()` 후에만 참이고 정상 셧다운은 drain 후 워커가 `run()`을 호출하지 않아 실질 영향 없음 → "run 호출 시 항상 기록"이 더 일관적이라 유지.
> - **네이밍 정리 (함께 적용)**: `drainMailbox`→`popMessage`(단일 pop 의미 명확화), `handleLifecycle`→`tryConsumeLifecycle`(반환 bool = "소비했는가" 명시), `moreWork`→`hasMoreWork`, Scheduler `timeMs`→`delayMs`(addTimer 파라미터 통일).

- [x] `BatchResult` 구조체 + `processBatch()` 분리 (메시지 루프·라이프사이클·예외만) ✅
- [x] `run()` → `processBatch` + 측정·메트릭·재디스패치 래퍼로 얇게 (공개 시그니처 `int run(int,bool*)` 불변 — 기존 호출부 무변경) ✅
- [x] `stopped_`·타이머 해제·레지스트리 해제 등 기존 로직 유지 확인 (`test_actor_runtime` 통과) ✅

#### 1.4.4 라이프사이클 처리 (인라인 유지 vs 별도 핸들러 — 결정 필요)
라이프사이클(`ActorEnable/Disable/RestartRequest`) 처리는 현재 `ActorRuntime`의 **private 메서드**(`tryConsumeLifecycle`/`performRestart`)로 인라인되어 있고, `ActorRestartRequest`는 **OneForAll 브로드캐스트** 동작(Opened 액터만 재시작, `restartCount_` 증가 없음)을 주석으로 명시한다. 이는 OneForOne(`tryRestart`, `restartCount_` CAS)와 구분되어 유지돼야 한다.

분리 옵션:
- **A (기본, 권장): 인라인 유지** — 로직이 25줄 미만이고 Actor의 상태에 직결되어 있어, 지금 추출은 오버엔지니어링. 1.5 종료 후 필요 시 재평가
- **B (선택): `ILifecycleHandler` 추출** — 이후 라이프사이클 정책이 다양해질 때만. `i_lifecycle_handler.hpp`(co-location) + `default_lifecycle_handler.hpp/cpp`

```cpp
// 현재: ActorRuntime private 메서드 (출처: actor_runtime.cpp)
bool ActorRuntime::tryConsumeLifecycle(const Message& msg){
    switch(msg.id()){
    case MessageId::ActorEnableRequest:
        if(actor_->getState() == Closed) actor_->open();
        return true;
    case MessageId::ActorDisableRequest:
        if(actor_->getState() == Opened && !actor_->isEssential()) actor_->close();
        return true;
    case MessageId::ActorRestartRequest:        // OneForAll 브로드캐스트
        if(actor_->getState() == Opened) performRestart(msg.as<ActorRestartRequest>().reason);
        return true;
    default:
        return false;
    }
}
```

- [x] **결정**: (A) 인라인 유지로 1.4 마무리 확정 — 로직 25줄 미만 + Actor 상태(`getState`/`open`/`close`) 직결, 분리 시 오히려 Actor↔핸들러 통신 계층만 추가됨(과설계, YAGNI). 1.5 종료 후 재평가 ✅
- [x] `tryRestart`(OneForOne, restartCount_ CAS) ↔ `tryConsumeLifecycle`(OneForAll) **구분 주석 유지** — `actor_runtime.cpp:70-76`(OneForAll 브로드캐스트, restartCount_ 미증가) + `:84-92`(OneForOne, CAS) 주석 존재 확인 ✅
- [x] 라이프사이클 롤백 없이 동작 보존 (`test_actor_runtime`) — 코드 변경 0이므로 동작 보존; 테스트 실행은 빌드 후 확인 ✅

### 1.5 전역 상태 제거: Metrics, Log, MemoryPool (P0-5, P0-6)

#### 1.5.1 `Metrics` → 인스턴스 기반 ✅

> **접근: 상태 인스턴스화 + 활성 핸들 (cross-cutting, 로그와 동일 패턴)**. Metrics는 관찰성(cross-cutting) 기능이므로 ctor 주입 대신, **상태는 `Metrics` 인스턴스에, 접근은 전역 활성 핸들 `activeMetrics()`** 로. (`v2_core_smoke` 등 미설정 환경은 내부 fallback 인스턴스로 폴백 → null 안전).
> - **호출부 편의 매크로**: `#define V2_METRICS() (&activeMetrics())` → `V2_METRICS()->recordX(...)` (`->` 문법). 로그 `V2_LOG_*`와 같은 정신.
> - **`isEnabled()` 가드 제거**: `record*`가 내부 첫 줄에서 `if(!enabled_) return;` 하므로 외곽 가드는 삭제(호출부 1줄로 단순화).
> - **복사·이동 금지 유지** (단일 인스턴스 → 카운터 이중화·활성 핸들 무효화 방지).
> - **`IMetrics`(1.2.4)는 지금 안 붙임(YAGNI)**: 구현체 1개뿐이고 시그니처가 실구현과 상이(`recordDispatch` dedup 누락, `snapshot` 구조 차이). 두 번째 구현체(예: prometheus exporter, Phase 2) 등장 시 `activeMetrics()`를 `IMetrics&` 반환으로 승격 + 인터페이스 보정.
> - **Metrics 인스턴스 소유**: Composition Root(main_app) — 멤버 `Metrics metrics_` + `setActiveMetrics(&metrics_)`. bench는 별도 연결 없이 fallback(enabled=false) 사용.

```cpp
// src/core/perf/metrics/metrics.hpp
class Metrics{                      // IMetrics 미구현 (보류)
    bool enabled_{false};           // static 제거 → 인스턴스 멤버
    std::vector<std::unique_ptr<ActorMetrics>> actors_;
    std::vector<std::unique_ptr<WorkerMetrics>> workers_;
    DispatcherMetrics dispatcher_;
public:
    explicit Metrics(size_t numWorkers = 0);   // copy/move = delete
    // ... record*/snapshot/reset/init ...
};

Metrics& activeMetrics();         // 활성 핸들 (fallback 폴백)
void setActiveMetrics(Metrics* m);
void clearActiveMetrics();
#define V2_METRICS() (&activeMetrics())        // 호출부 편의 매크로
```

- [x] static 멤버/메서드 제거 → 인스턴스 멤버 + `explicit Metrics(size_t)`, copy/move delete 유지 ✅
- [x] `activeMetrics()/setActiveMetrics()/clearActiveMetrics()` 활성 핸들 + `V2_METRICS()` 매크로 ✅
- [x] 호출부 치환: `Metrics::X` → `V2_METRICS()->X`, `if(isEnabled())` 가드 제거 (actor_system/actor_runtime/worker/work_dispatcher/cmd_actor) ✅
- [x] Composition Root: `main_app` 멤버 `Metrics metrics_` + `setActiveMetrics(&metrics_)` ✅
- [x] bench 6곳 + main `setEnabled` → 활성 핸들 경유 ✅
- [x] 검증: `rg 'Metrics::'` → src 정의부/문서 제외 0건, `i_metrics.hpp`는 미사용 유지 ✅

#### 1.5.2 `Log` → 인스턴스 기반 ✅
> **접근: 상태 인스턴스화 + 활성 핸들 (cross-cutting, 1.5.1과 동일 패턴)**. Log는 관찰성(cross-cutting) 기능이므로 ctor 주입 대신, **상태는 `Logger` 인스턴스에, 접근은 전역 활성 핸들 `activeLogger()`** 로. 미설정 환경(테스트/벤치)은 내부 `fallbackLogger()`로 폴백 → null 안전.
> - **`ILogger`(1.2.3)는 안 붙임(YAGNI)**: 구현체 1개뿐이고 시그니처가 실구현과 상이. 두 번째 구현체(Phase 2.4 로거 구현체) 등장 시 `activeLogger()`를 인터페이스 반환으로 승격.
> - **LogLevel 6단계**: `Verbose=0, Debug, Info, Warn, Error, Fatal` (기존 4단계 확장). 색상: Verbose=흰색, Debug=회색, Info=시안, Warn=노랑, Error=빨강, Fatal=굵은 빨강.
> - **`LogLevel` 소속을 `log.hpp`로 이관** → `i_logger.hpp` 삭제.
> - 복사·이동 금지 유지 (단일 인스턴스 → 활성 핸들 무효화 방지).

```cpp
// src/core/common/log/log.hpp
enum class LogLevel : uint8_t { Verbose=0, Debug, Info, Warn, Error, Fatal };
class Logger{                         // ILogger 구현 보류
    FILE* logFile_ = nullptr;
    std::mutex mutex_;
    std::atomic<LogLevel> level_{LogLevel::Info};
public:
    Logger() = default;               // copy/move = delete
    void log(LogLevel, file, line, func, msg);   // thread_local gBuf 누적, ≥512B 시 flush
    void logBlock(std::string_view);             // 원본 그대로 stderr + logFile 즉시 write + flush (크래시/팬릭 박스)
    void setLevel/getLevel/setLogFile/flushBuffer;
};
Logger& activeLogger();   void setActiveLogger(Logger*);   void clearActiveLogger();
#define V2_LOGGER() (&activeLogger())                      // 접근자 편의 매크로
#define V2_LOG_VERBOSE/DEBUG/INFO/WARN/ERROR(...) activeLogger().log(...)
#define V2_LOG_FATAL(...) do{ log(Fatal, ...); flushBuffer(); }while(0)  // abort 전 버퍼 flush 보장
```

- [x] `i_logger.hpp` 제거, `LogLevel` 소속 `log.hpp` 이관, copy/move delete ✅
- [x] static 전역(`gLevel/gLogFile/gMutex`) → 인스턴스 멤버 + `activeLogger/setActiveLogger/clearActiveLogger` + fallback ✅
- [x] Composition Root(main/cli/tui): 멤버 `Logger logger_` + `setActiveLogger(&logger_)` + `setLevel/setLogFile` ✅
- [x] LogLevel 6단계 확장 + `V2_LOG_DEBUG`/`V2_LOG_VERBOSE` 신설 (기존 `V2_LOG_INFO/WARN/ERROR` 매크로 경유 — 호출부 무변경) ✅
- [x] `debug.hpp` ↔ `log.hpp` include 역전: `log.hpp`의 `debug.hpp` include 제거 → `debug.hpp`가 `log.hpp` 포함. **debug가 fatal 로그를 담당**(사용자 원안), 매크로 순환 없음 ✅
- [x] `V2_PANIC`/`V2_ASSERT` 박스(Message/Expression/File/Line/Function)를 `std::string`으로 조립해 `activeLogger().logBlock()` → **터미널 + `.log` 파일 동시 기록** ✅
- [x] `V2_LOG_FATAL`에 `flushBuffer()` → abort 전 버퍼 확실 flush (fatal/panic 내용이 `.log`에 잔존) ✅
- [x] `main_app.cpp`에 `#include "core/common/util/debug.hpp"` 추가 (include 역전으로 소실된 `V2_PANIC` 선언 복구) ✅
- [x] 참고: `V2_ASSERT`가 Debug에서도 발동하지 않던 원인 = 빌드가 Release 폴백(`-DNDEBUG`) — `CMakeLists.txt` 폴백을 Debug로 변경 후에도 **캐시의 `CMAKE_BUILD_TYPE`이 우선**하므로 `cmake -B build -DCMAKE_BUILD_TYPE=Debug`로 재구성 필요 ✅

> **검증 정책 (방향 3 확정) — 릴리즈에서 assert/panic 처리**
> - `V2_ASSERT`는 **개발용 불변식** 전용, NDEBUG 시 제거 (C 관용 유지) — 릴리즈 hot path 성능 부담 없음.
> - **런타임 필수 실패(외부 자원: epoll fd, eventfd 등)는 assert로 abort하지 않는다.** 리턴/전파로 복구하거나 graceful 종료 → **차기 에러 핸들링 리팩토링(Phase 4-1 supervision + 액터 에러 전파)의 몫**으로 연결.
> - abort는 **정말 복구 불가 시에만** `V2_PANIC`(NDEBUG 무관 항상 on) + 로그로 원인 남기고 즉사.
> - 지금 단계: `V2_ASSERT` 사용처(ring_buffer/event_loop)는 **유지하되 릴리즈 의존하지 말 것** — 디버그 가드일 뿐이라는 주석과 함께 이관 대상 목록으로 남김.

#### 1.5.3 `Message` → `IMemoryAllocator` 주입 ✅
```cpp
// src/core/actor_system/messages/message.hpp
class Message {
    // MemoryPool::instance() 제거 — 전역 싱글톤 제거 ✅
    IMemoryAllocator* allocator_ = nullptr;  // null → core 기본 풀 인스턴스로 폴백
    // allocator_ 는 move 시 함께 이전
    // ops_ 함수포인터가 IMemoryAllocator* 파라미터를 받도록 변경
};
```

- [x] `MemoryPool::instance()` 제거 → `defaultMemoryPool()` 모듈-로컬 폴백 (전역 접근 API 제거, 호출부 0건)
- [x] `Message::allocator_` 스냅샷 + ops에 `IMemoryAllocator*` 파라미터 (move 이전, inline은 풀 미사용)
- [x] pool 경로 할당 실패 시 `throw std::bad_alloc{}` (기존 `ThrowAllocPolicy` 동작 유지)
- **기본값은 core 풀 인스턴스** (`MemoryPoolT`를 인스턴스 기반으로 전환). core는 어떤 pool도 몰라도 동작 (v2_core_smoke 유지) ✅

> **소유권 판정 (구현 확정)**: 기본 소유 = core의 프로세스-수명 폴백 `defaultMemoryPool()`. **MessageFactory는 보류 (YAGNI)** — 현재 소비자(비-기본 풀 주입이 필요한 곳)가 없고 로드맵상 "선택"이라, 필요가 생기는 시점(2.3 arena 주입·테스트 격리)에 `Message::make(value, alloc)` 오버로드 + 팩토리 형태로 추가.

> **TLS 이관은 후속으로 연기 (B)**: 아래 항목(242행의 `inline thread_local tlCache_` — `thread_local_cache.hpp`)은 이번 1.5.3에 포함하지 않았다. 이유: C++에서 **비정적 멤버에 `thread_local`을 붙일 수 없어**(static/전역 전용) "풀 인스턴스 소유 thread cache"는 결국 `thread_local` 레지스트리(예: `map<풀*,캐시>`) 재설계가 필요하고, 이는 여전히 스레드별 전역 구조라 '전역 제거'에 트레이드오프가 있다. hot path(성능)에 영향 가능 — **2.3 arena 논의와 함께 별도 리팩토링으로 변경**.

> **참고 (후속)**: pool 경로가 raw 인터페이스를 쓰므로 `MemoryPoolT`의 `DebugPolicy` 후크가 메시지 할당 경계에서 bypass됨 (기본 `NoDebugPolicy`라 동작 동일, `DebugMemoryPool` 사용 시 poison 미적용) — 2.3에서 정리.

#### 1.5.4 메시지 생성 은닉 API (Payload-전용 send/sendMsg 오버로드) ✅ (1.5.3과 함께 구현)
> **동기**: `actor.send(target, Message::make(CmdRequest{...}))`처럼 호출자가 매번 `Message::make`를 쓰는 것을 없애고, **payload 타입만 넘기면 send 내부에서 `Message` 생성(그리고 그 안의 allocator 결정)을 은닉**한다. 1.5.3의 allocator 주입과 강하게 연관 — 주입 결정 지점이 은닉 레이어 안에만 생기면 되므로 **1.5.3과 함께 진행 권장**.
> **id 자동 추론**: 각 메시지 타입은 `static constexpr MessageId kId`를 보유 (`message_traits.hpp` + 각 messages 헤더). payload 타입만으로 `DT::kId`가 추론되므로 **호출자가 id를 알 필요 없음**.

```cpp
// src/core/actor_system/actor/actor.hpp (+ actor_handle.hpp의 ActorHandle::send)
template<typename M, typename = std::enable_if_t<!std::is_same_v<std::decay_t<M>, Message>>>
void sendMsg(const std::string& name, M&& msg){
    sendMsg(name, Message::make(std::forward<M>(msg)));   // payload → Message 내부 변환
}
// sendMsg(uint64_t), sendMsgAfter(name/id, delay), receiveMsg, startTimer 동일 패턴
```
- caller: `sendMsg("target", CmdRequest{...})` — `Message` 생성 완전 은닉
- `Message` 직접 전달 → **non-template 오버로드가 선택**되어 기존 호출(테스트/bench 포함 수백 곳) 호환 유지 (SFINAE로 템플릿이 `Message`를 안 잡게 함)
- 오버로드 규칙: `Message` 객체 → non-template 승리 / payload 타입 → 템플릿이 `Message::make`로 감쌈
- **의도한 아님 폼**: `send(id, args...)` (id + 개별 인자)는 메시지마다 생성 인자가 달라 **일반화 불가** → payload 구조체를 넘기는 방식만 허용

- [x] `Actor`의 sendMsg × 2, sendMsgAfter × 2, receiveMsg, startTimer + `ActorHandle::send`에 payload 템플릿 오버로드 추가 (첨가형, 기존 호출 무변경)
- [x] 도메인(`service/*`)의 핫 루트 호출을 payload 형태로 전환해 `Message::make` 누들을 정리 — allocator 주입(1.5.3)과 함께 처리
  - 전환 범위: send/sendMsg/sendMsgAfter/startTimer/h.send (cmd·network_manager·monitor·tick·ipc·device_manager·dbus)
  - **유지**: `ctx->enqueue(...)`/`runtime()->enqueue(...)`는 가상 인터페이스 경유라 payload 오버로드가 어색 → `Message::make` 공용 API로 유지

### 1.6 서비스 계층 경계 복원 — 포트 소유권 정리 (P0-7)

> **현재 상태 (2026-08 점검 기준)**: 생성자 주입·composition root 이관은 **이미 완료**. 잔여 작업은 **포트 파일 위치 정리**와 **service의 nlohmann(3rd-party) 의존 제거** 두 가지.
> - ✅ `cmd_actor`(`IPmu*`), `monitor_actor`(`ISys*`+`IPmu*`)는 이미 생성자 주입형 — 하단 Before `#if` 예시는 현재 코드와 불일치하므로 삭제
> - ✅ 플랫폼 분기 선택은 `main_app.cpp::registerServices()`에서 DI 컨테이너(`ServiceContainer::bind<IPmu, PmuRsp5/PmuMock>` 등)로 수행됨
> - ⚠️ `IPmu`/`ISys`/`II2c` 인터페이스가 여전히 `infra/hal/{pmu,sys,i2c}/`에 있고, `service/ports/`는 **0-byte placeholder 3개**만 존재
> - ⚠️ `monitor_data.hpp`가 `<nlohmann/json.hpp>` + `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` 매크로 보유 — service가 3rd-party 직접 의존
> - ⚠️ `monitor_actor`가 `UdsServer`(infra) 직접 사용 — **보류 결정** (아래 참조, 별도 종합 설계로 이관)

**문제**: `IPmu`/`ISys`/`II2c` 인터페이스가 `infra/hal/`에 있어 service가 infra의 include 경로를 참조. `monitor_data.hpp`의 nlohmann include로 service가 3rd-party에 직접 의존.

**파일** (인터페이스만 이동 — 구현체는 infra 유지):
- `src/infra/hal/pmu/i_pmu.hpp` → `src/service/ports/i_pmu.hpp`
- `src/infra/hal/sys/i_sys.hpp` → `src/service/ports/i_sys.hpp`
- `src/infra/hal/i2c/i_i2c.hpp` → `src/service/ports/i_i2c.hpp`

> **의존 방향 함의 (중요)**: 포트가 service 소유가 되면 구현체(infra)가 service 포트 헤더를 include = **infra→service 헤더 의존** 발생. 포트를 **header-only로 유지**하고 두 타깃(`infra.cmake`/`service.cmake`)이 모두 `src/`를 include 경로로 보유하므로 **CMake 링크 변경 불필요** (static 링크 사이클 없음). 반드시 포트에 `.cpp`를 만들지 말 것. 36행 다이어그램에 이 예외를 반영해 둠.

**리팩토링** (구현 선택/주입은 이미 완료 — 참고용):
```cpp
// After (완료 상태): 액터는 인터페이스만 생성자 주입, 구현 선택은 composition root가 담당
class CmdActor : public Actor {
public:
    CmdActor(std::string name, uint64_t id, IPmu* pmu)
        : Actor(std::move(name), id), pmu_(pmu) {}
private:
    IPmu* pmu_;   // 소유하지 않음 (수명 = main_app 멤버)
};
```

- [x] `cmd_actor`/`monitor_actor` 생성자 주입 전환 (`IPmu*`/`ISys*`) ✅
- [x] 플랫폼 분기 → `main_app.cpp::registerServices()` DI bind로 이동 ✅
- [ ] **3개 HAL 포트 `service/ports/`로 이동** + include 경로 수정
  - 구현체 include: `infra/hal/pmu/pmu_rsp5.hpp`·`pmu_mock.hpp`, `infra/hal/sys/sys_linux.hpp`·`sys_mock.hpp`, `infra/hal/i2c/i2c_linux.hpp` → `"service/ports/*.hpp"`
  - 소비자 include: `service/cmd/cmd_actor.hpp`, `service/monitor/monitor_actor.hpp`, `app/main/main_app.hpp` → `"service/ports/*.hpp"`
  - `main_app.cpp`의 구현체 include(`pmu_mock.hpp` 등)는 composition root이므로 유지
  - `II2c`는 현재 소비자 없음(dead) — 로드맵대로 이동만 (`i_i2c.hpp`의 `transfer()` 비순수 가상은 스코프 밖)
- [ ] **`monitor_data.hpp` nlohmann 제거** (순수 POCO) + infra JSON 코덱으로 직렬화 위임
  - 신설 `infra/transport/monitor_json_serializer.hpp/.cpp` — `monitorJson::serialize(const MonitorSnapshot&)` / `deserialize(std::string_view)` (JSON 키 = 기존 필드명 유지 → TUI/CLI 호환). `infra.cmake` 소스 목록에 추가 (nlohmann은 이미 PUBLIC 링크)
  - `MonitorActor` 생성자에 `std::function<std::string(const MonitorSnapshot&)> serializer` 주입 (새 인터페이스 파일 없이 가장 가벼움 — 1.5 YAGNI 기조와 일치). empty 시 broadcast skip + 경고 로그
  - `main_app.cpp`에서 람다로 배선, `tui_app.cpp` 파싱(`nlohmann::json::parse`)을 코덱 경유로 전환
- [ ] **보류 (별도 종합 설계로 분리)**: `monitor_actor`의 `UdsServer` 직접 사용 제거 → `IIpcServer` 포트
  - 사유: raw `::recv`/`::close`/`::chmod` 잔존으로 OS 결합이 반만 제거됨 + 동일 패턴의 `ipc_server_actor`/`system_actor`(signal_handler)까지 포함한 **"service↔infra 직접 include 0개"(M5b) 목표의 일부**로 통합 검토가 올바름. 1.6에서 부분 진행하지 않음

**검증**: `rg 'infra/hal/pmu|infra/hal/sys|infra/hal/i2c' src/service` → 0, `rg 'nlohmann' src/service` → 0

### 1.7 서비스 전용 메시지 이관 (P0-8)

> **현재 상태 (2026-08 점검 기준)**: 메시지 파일 이관은 **완료**. `core/actor_system/messages/`에는 `message.hpp`/`message_traits.hpp`/`system_messages.hpp` 3개만 남음. 잔여 과제는 **`MessageId` enum에 남은 서비스 ID**(Phase 3.4에서 해소)와 CI 스캔뿐.

**원칙**: 메시지도 Port처럼 **소유 도메인(서비스) 폴더에 co-location**. core는 엔진 범용 타입만 유지.

**이관 완료 확인**:
- ✅ `cmd_messages.hpp` → `service/cmd/cmd_messages.hpp`
- ✅ `ipc_messages.hpp` → `service/ipc/ipc_messages.hpp`
- ✅ `dbus_messages.hpp` → `service/dbus/dbus_messages.hpp`
- ✅ `monitor_messages.hpp` → `service/monitor/monitor_messages.hpp`
- ✅ `tick_messages.hpp` → `service/tick/tick_messages.hpp`
- ✅ `device_manager_messages.hpp` → `service/device_manager/device_manager_messages.hpp`
- ✅ `network_manager_messages.hpp`, `wifi_messages.hpp` → `service/network_manager/`

**core 유지 확인**:
- ✅ `message.hpp`(envelope) — 서비스 타입 미참조 (`DT::kId` 트레이트 + 인라인/풀 스토리지만 사용)
- ✅ `system_messages.hpp`(엔진 수명주기 — ActorEnable/Disable/Restart)
- ⚠️ `message_traits.hpp` — **`MessageId` enum에 서비스 ID 잔존**: `Tick`, `Ipc*`, `Monitor*`, `Dbus*`, `Device*`, `Cmd*`, `Nm*`, `Wifi*`

> **잔여 결합**: 각 서비스 메시지가 `static constexpr MessageId kId`로 enum 값을 참조하므로 `MessageId` enum은 이동이 불가능한 상태. 완전 해소는 **Phase 3.4**(타입 안전 `MessageId` + `MessageId::user<V>()` 템플릿 + `V2_MESSAGE_TRAITS` 매크로로 서비스가 자체 ID 정의)의 몫 — 1.7에서 무리하게 진행하지 않음. 이 시점의 "core가 서비스 도메인을 앎"은 파일 단위가 아니라 **enum 값 단위**로 축소됨.

**참고**: 서비스 간 공유 메시지는 "소유 서비스가 원천"이고 소비자는 해당 헤더만 include (데이터 계약이라 허용) — 예: `network_manager_actor.cpp`의 `tick/tick_messages.hpp`, `cmd_actor.hpp`의 `wifi_messages.hpp`, `ipc_server_actor.cpp`의 `cmd/cmd_messages.hpp`.

- [x] 각 서비스 메시지 파일 이관 + 모든 `#include` 경로 수정 ✅
- [x] 이관 후 `core/actor_system/messages/`에 서비스 **메시지 파일**이 안 남는지 검증 (grep 확인 — 파일 3개뿐) ✅ / `MessageId` enum의 서비스 ID는 3.4에서 해소
- [ ] (선택) CI 스캔: `core/actor_system/messages/`에 서비스 도메인 파일 추가 시 실패하는 grep 스크립트 등록 (1.8 스캔과 함께 구성)

### 1.8 서비스 간 결합 완화 (P0-9)

> **현재 상태 (2026-08 점검 기준)**: 서비스 간 include는 **데이터 계약 위반 0건, 구체 액터 타입 결합 1건**만 존재.
> - 허용 (데이터 계약): `network_manager_actor.cpp`→`tick/tick_messages.hpp`, `cmd_actor.hpp`→`wifi_messages.hpp`, `ipc_server_actor.cpp`→`cmd/cmd_messages.hpp`, `cmd_actor.cpp`→`monitor/monitor_data.hpp`
> - **위반 1건**: `network_manager_actor.cpp:10` `#include "service/dbus/dbus_actor.hpp"` + `dynamic_cast<DbusActor*>` + `dbus->connection()` 직접 호출 (sdbus 연결 객체 요구)

**문제**: `network_manager_actor.cpp`가 `service/dbus/dbus_actor.hpp`를 직접 include (구체 액터 타입 결합). `findByName()`+`dynamic_cast`로 dbus 액터를 찾아 `sdbus::IConnection&`를 탈취.

**원칙**: 서비스 간 통신은 메시지/레지스트리 조회로. 구체 액터 헤더 include 금지.
**중요**: **"메시지 기반 통신으로 전환"은 이 사례에 부적합** — 네트워크 매니저가 필요로 하는 건 `sdbus::IConnection&`(3rd-party 연결 객체)인데, 이는 메시지 payload로 전달할 수 없음. 올바른 해법은 **소비자 소유 포트의 생성자 주입** (1.6 포트 소유권과 동일 원칙).

**리팩토링** (포트 주입으로 교정):
```cpp
// Before: network_manager_actor.cpp — 구체 액터 조회 + dynamic_cast + 연결 탈취
auto dbusHandle = runtime()->actorRegistry()->findByName("dbus_actor");
auto* dbus = dynamic_cast<DbusActor*>(dbusHandle.get());
if(!dbus || dbus->getState() != Opened){ return Fail; }
connection_ = &dbus->connection();                     // sdbus::IConnection& 직접 노출

// After: service/dbus 소유 포트 — 구현 어댑터가 DbusActor의 connection을 감쌈
class IDbusConnection {                                // service/dbus/i_dbus_connection.hpp (co-location)
public:
    virtual ~IDbusConnection() = default;
    virtual bool ready() const = 0;                    // = dbus_actor Opened 여부
    virtual sdbus::IConnection& connection() = 0;      // sdbus 타입 노출은 수용 (service는 sdbus 링크 중)
};
NetworkManagerActor(std::string name, uint64_t id, IDbusConnection* dbus);
```

- [ ] `service/dbus/i_dbus_connection.hpp` 포트 신설 (Phase 2의 `i_dbus_handler.hpp` 계획과 통합 검토)
- [ ] `network_manager_actor` → `IDbusConnection*` 생성자 주입으로 전환, `findByName`+`dynamic_cast`+`dbus->connection()` 제거 (registry 조회는 신원 조회 목적으로만 유지)
- [ ] composition root(`main_app.cpp`)에서 dbus 어댑터 생성·주입 (라이프사이클: dbus 액터 open 순서와 ready()로 정합)
- [ ] 서비스 디렉토리 간 `#include` 스캔: **데이터 계약(메시지/POCO)은 허용**, **구체 액터 헤더는 위반** — CI 스크립트로 검출 (위반 시 실패). 1.7의 core 메시지 잔여 스캔과 함께 구성

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
│   ├── memory/                              # 특수/외부 할당자만 (기본 풀은 core 유지 — 1.2.2)
│   │   └── linux_arena.hpp/cpp              # 예시: hugepage/mmap arena
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

### 2.3 메모리 할당자 구현체 — 특수/외부 전략만 (기본 풀은 core 유지)

> **수정됨 (1.2 설계 확정에 따라)**: 기본 portable 풀(`MemoryPoolT`, slab/TLS)은 **core에 유지**한다. infra에는 **플랫폼/외부 라이브러리 의존 특수 전략**만 둔다 — 1.2.2 소유권 결정 참고.

#### 2.3.1 `infra/memory/` 특수 할당자 (예시)
```cpp
#include "core/common/memory/i_memory_allocator.hpp"

// 예시 1: Linux hugepage/mmap 기반 arena (플랫폼 의존 → infra)
class LinuxArenaAllocator : public IMemoryAllocator {
    // mmap + madvise 등 Linux API 사용
};

// 예시 2: 외부 라이브러리 래퍼 (jemalloc/tcmalloc 등 → infra 링크)
class JemallocAllocator : public IMemoryAllocator {
    // ::malloc 등 외부 심볼 래핑
};
```

- 기본/대안 선택은 Composition Root가 함 (core 기본 풀 vs infra 특수 풀)

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
    while (runtime->popMessage(msg)) {
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

**파일**: `src/core/CMakeLists.txt`

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
| **M5b: 서비스 계층 경계 복원** | Week 6 말 | `service/ports/` 포트 이관 + service nlohmann 0건 (1.6), 서비스 메시지 이관 완료 (1.7), dbus 결합 포트 주입으로 해소 (1.8). "service↔infra 직접 include 0개"는 보류된 UDS/signal_handler 종합 설계 후 달성 |
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
| `core/common/memory/memory_pool.hpp` | `core/common/memory/i_memory_allocator.hpp`(포트) + **MemoryPoolT는 core 유지** (인스턴스 기반 전환), infra는 특수 할당자만 | 인터페이스/구현 분리 + 싱글톤 제거 |
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
| `src/core/CMakeLists.txt` | `src/core/CMakeLists.txt` (독립 subproject) + `infra/infra.cmake` (구현체) | CMake 분리 |
| `service/network_manager/network_manager_actor.cpp` | (수정) `dbus_actor.hpp` 직접 include 제거 | 메시지 기반 통신으로 전환 |

---

*문서 버전: 1.0*
*작성일: 2026-08-01*
*검토자: Software Architect*