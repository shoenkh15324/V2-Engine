# 코어 계층 (`src/core/`)

V² Engine의 심장부인 **코어 계층**의 전체 지도. 무엇이 어디 있고, 어떤 규칙으로 지켜지고, 각 주제를 어느 문서에서 깊게 읽을 수 있는지 처음 오신 분도 한눈에 따라올 수 있게 정리한 문서.

---

## 목차

- [개요](#개요)
- [설계 원칙](#설계-원칙)
- [디렉터리 지도](#디렉터리-지도)
- [주제별 문서 찾기](#주제별-문서-찾기)
- [빌드 구조](#빌드-구조)
- [의존성 규칙](#의존성-규칙)
- [포트 정의 — 외부 세계와의 약속](#포트-정의--외부-세계와의-약속)

---

## 개요

코어 계층은 **외부 의존성이 제로인 자체 완결형 C++20 서브프로젝트**입니다.
액터 프레임워크, 락프리 큐, 메모리 풀, 스케줄러, 슈퍼바이저 같은 엔진의 모든 핵심 메커니즘이 여기 살고 있습니다.

**자체 완결형**의 의미:

- `epoll`, 소켓, timerfd 같은 **시스템 콜이 전혀 없습니다**
- nlohmann/json, FTXUI 같은 **서드파티 라이브러리도 없습니다**
- 유일하게 링크하는 것은 C++ 표준 스레드 라이브러리(`Threads::Threads`)뿐입니다

덕분에 코어는 macOS·Windows에서도 빌드되고, OS 없는 환경과 동일한 조건으로 단위 테스트할 수 있습니다. OS 의존 기능은 전부 [인프라 계층](../../concepts/infrastructure.md)이 대신하며, 그 연결 규약은 아래 [포트 정의](#포트-정의--외부-세계와의-약속) 절에서 다룹니다.

---

## 설계 원칙

| 원칙 | 설명 |
|------|------|
| **핫 패스 락프리** | 메시지 큐잉→디스패치→획득→처리 경로에는 뮤텍스가 없습니다. |
| **액터 격리** | 액터는 공유 상태 없이 메시지 전달로만 통신합니다. |
| **의존성 역전** | OS 기능이 필요하면 코어가 *인터페이스(포트)*를 선언하고, 인프라가 *구현체(어댑터)*를 꽂습니다. |
| **정책 주입** | 로그 레벨, 워커 수, 재시작 예산 등 모든 동작 파라미터를 외부 설정으로 주입받습니다. |
| **관측 가능성** | 모든 중요 경로에 relaxed 원자 카운터가 붙어 성능 문제를 추측이 아닌 숫자로 진단합니다. |

---

## 디렉터리 지도

```
src/core/
├── actor_system/            ★ 액터 프레임워크 본체
│   ├── actor_system.hpp/cpp    최상위 조립체 (ActorSystem)
│   ├── actor/                  Actor, ActorHandle, ActorRegistry
│   ├── messages/               Message, MessageOps, MessageId, core_messages
│   ├── runtime/
│   │   ├── actor_runtime/      ActorRuntime (배치 처리, 재시작)
│   │   ├── dispatcher/         WorkDispatcher + Worker (실행 토큰, 작업 스틸링)
│   │   │   └── io/             IEventLoop 포트
│   │   ├── mailbox/            Mailbox (MPSC 래퍼)
│   │   ├── scheduler/          Scheduler (타이머 콜백 연결)
│   │   └── supervisor/         Supervisor, DeadLetterQueue
│   └── detail/                 (예비 공간)
├── common/                  공통 유틸리티 → [common.md](common.md)
│   ├── container/              MPSC/MPMC 락프리 큐, RingBuffer
│   ├── memory/                 MemoryPool (TCMalloc 스타일 3계층)
│   ├── log/                    Logger
│   ├── time/                   Time, Sleep
│   ├── timer/                  TimerBase, Timer(std), ITimer 포트
│   ├── di/                     ServiceContainer (DI)
│   ├── config/                 플랫폼 감지 매크로, RuntimeConfig
│   └── util/                   Result 코드, V2_ASSERT/V2_PANIC
└── perf/
    └── metrics/                성능 메트릭 → [metrics.md](metrics.md)
```

> `actor_system/detail/`은 현재 비어 있는 예비 공간입니다.

---

## 주제별 문서 찾기

코어의 심층 문서는 두 곳에 나뉩니다 — **개념(concepts)** 문서와 이 폴더(**layers**) 문서입니다.

### concepts 문서 (메커니즘 심층 분석)

| 주제 | 문서 | 다루는 내용 |
|------|------|-------------|
| 액터 모델 | [actor_model.md](../../concepts/actor_model.md) | Actor 생명주기, ActorSystem, 세대 기반 ActorHandle, 타입 안전 dispatch |
| 메시지 시스템 | [messaging.md](../../concepts/messaging.md) | 96바이트 Message 레이아웃, SBO, 40개 메시지 카탈로그, 통신 패턴 |
| 스케줄링 | [work_dispatch.md](../../concepts/work_dispatch.md) | 실행 토큰, inFlight 슬롯, finalize 정산, 스핀-던-파크, 성능 케이스 스터디 |
| 동시성 | [concurrency.md](../../concepts/concurrency.md) | MPSC/MPMC 큐, 작업 스틸링, 메모리 오더링 전략 |
| 메모리 관리 | [memory.md](../../concepts/memory.md) | 3계층 풀 할당자, SizeClass, Slab |
| 슈퍼바이저 | [supervision.md](../../concepts/supervision.md) | 재시작 전략, DeadLetterQueue |

### layers/core 문서 (계층 내부 유틸리티)

| 주제 | 문서 | 다루는 내용 |
|------|------|-------------|
| 액터 시스템 조립·생명주기 | [actor_system.md](actor_system.md) | createDefaultActorSystem 조립, start/stop 시퀀스, ActorRuntime 견고성(드롭·예외 격리·재시작) |
| 레지스트리·세대 핸들 | [registry_handle.md](registry_handle.md) | 세대 카운터로 use-after-free 방지, 이름 충돌 규칙, 스냅샷 순회 |
| 타이머 스케줄러 | [scheduler.md](scheduler.md) | Scheduler/TimerBase 역할 분담, clone 배달, 취소·정리 경로 |
| 공통 유틸리티 | [common.md](common.md) | Logger, Time/Sleep, DI 컨테이너, 플랫폼 설정, Result/assert, RingBuffer |
| 성능 메트릭 | [metrics.md](metrics.md) | ActorMetrics/WorkerMetrics/DispatcherMetrics, V2_METRICS(), 벤치마크 연계 |

---

## 빌드 구조

**파일:** `src/core/CMakeLists.txt`

```cmake
project(V2_CORE LANGUAGES CXX)
add_library(v2_core OBJECT ...)
find_package(Threads REQUIRED)
target_link_libraries(v2_core PUBLIC Threads::Threads)
```

- **OBJECT 라이브러리** — 최종 실행 파일(v2_main, v2_cli, v2_tui)에 직접 합쳐집니다. 별도 .so/.a를 만들지 않아 링크 오버헤드가 없습니다.
- **유일한 의존성 `Threads::Threads`** — `std::thread`, `std::counting_semaphore` 같은 C++20 스레드 기능을 위한 플랫폼 링크 옵션(-pthread) 확보용입니다.
- **include 경로** — `src/`가 public include 루트라서 어디서든 `#include "core/common/log/log.hpp"` 형태로 임포트합니다.
- 상위 `CMakeLists.txt`가 FetchContent로 가져오는 FTXUI/nlohmann-json/sdbus-c++는 **전부 인프라·애플리케이션 계층 전용**이며 코어 타깃에는 들어가지 않습니다.

---

## 의존성 규칙

코어에서 작업할 때 지키는 규칙:

| 규칙 | 이유 |
|------|------|
| POSIX/Win32 시스템 콜 호출 금지 | OS 독립성 유지 — epoll 등은 [인프라](../../concepts/infrastructure.md) 어댑터로만 |
| 서드파티 라이브러리 포함 금지 | 빌드 시간·휴발성 관리; 필요한 추상화는 직접 구현 |
| `src/infra/`, `src/service/` include 금지 | 의존성은 항상 바깥→안쪽. 코어는 아무것도 모름 |
| 예외 대신 `Result` 코드 반환 (경계에서) | 인프라 어댑터와의 계약 단순화 ([common.md](common.md)) |
| 실패 불가능한 불변식은 `V2_ASSERT`/`V2_PANIC`로 명시 | 조용히 잘못 진행하는 것보다 즉시 멈춤이 안전 ([common.md](common.md)) |
| 새 원자 변수는 메모리 오더링 근거를 주석으로 남김 | [동시성 문서](../../concepts/concurrency.md)의 오더링 표와 일관 유지 |

---

## 포트 정의 — 외부 세계와의 약속

코어는 OS 기능이 필요할 때 **포트**(순수 가상 인터페이스)를 선언하고, 실제 구현은 인프라 계층에 맡깁니다. 코어 안에 사는 포트 목록:

| 포트 | 위치 | 역할 | 구현체 (인프라) |
|------|------|------|------------------|
| `IEventLoop` | `actor_system/runtime/dispatcher/io/i_event_loop.hpp` | fd 감시·작업 post | `EventLoopEpoll` |
| `ITimer` | `common/timer/i_timer.hpp` | 타이머 등록·만료 알림 | `LinuxTimer` (timerfd) / `Timer` (std 폴백) |
| `IMemoryAllocator` | `common/memory/i_memory_allocator.hpp` | 할당/해제·통계 | `MemoryPool` (내장) 또는 외부 교체 |

`ActorSystemDeps` 구조체(`actor_system.hpp:37`)가 이런 의존성을 한 곳에 모아 `createDefaultActorSystem()`으로 주입합니다 — 조립은 애플리케이션 계층의 몫이라는 [육각형 아키텍처](../../concepts/infrastructure.md) 원칙입니다.
