# 🚀 V2 Engine (Visionary Vision Engine)

> 경량화된 C++17 액터 모델 엔진 — **고성능 동시성 서비스** 구축을 목표로 합니다.
>
> **핵심 목표:** 전통적인 lock 기반 동시성에서 벗어나, **액터 모델 + 이벤트 기반 아키텍처**를 통해
> 확장 가능하고 예측 가능한 멀티스레드 애플리케이션을 개발할 수 있는 엔진을 만드는 것.

---

## 🧱 아키텍처 개요

```
┌─────────────────────────────────────────────┐
│  src/app/      Application (demo, cli)      │
├─────────────────────────────────────────────┤
│  src/service/  Business Services (IPC)      │
├────────────────┬────────────────────────────┤
│  src/core/     │  src/infra/                │
│ Actor System   │  Transport · HAL           │
│ Common Utils   │  (UDS, epoll wrapper)      │
└────────────────┴────────────────────────────┘
```

4개의 **정적 라이브러리** 레이어로 구성되며, 의존성은 **단방향**으로 흐릅니다.
- `v2_core` ← `v2_service` ← `v2_app`
- `v2_infra` ← `v2_app`

---

## ⚙️ 주요 특징

| 특징 | 설명 |
|------|------|
| **Actor Model** | 경량 액터 + bounded mailbox (ring buffer), 비동기 메시지 패싱 |
| **std::variant 메시지** | 타입-safe 디스패치 — `std::visit` 기반, 상속/형변환 불필요 |
| **Thread Pool Dispatcher** | Worker N개가 ready queue에서 액터를 꺼내 실행 (협력적 스케줄링) |
| **epoll Event Loop** | `epoll_wait()` 기반 중앙 리액터 — Timer FD, Stop FD, Transport I/O 통합 |
| **Timer/Scheduler** | `timerfd_create()` + priority queue, `sendAfter()`/`startTimer()`/`cancelTimer()` |
| **Unix Domain Socket** | `UdsServer` / `UdsClient` (AF_UNIX, SOCK_STREAM) |
| **인터페이스 분리** | `IActorRegistry`, `IScheduler` — 테스트 용이성, 느슨한 결합 |
| **RAII / Move Semantics** | 복사 금지, 명시적 move only — 자원 소유권 명확 |
| **유틸리티** | Logging (ANSI color), RingBuffer, Semaphore, Time helpers, Assert/Panic 매크로 |

---

## 🗺️ 로드맵 (당장 할 것들)

- [ ] **IPC 서비스 구현** — `IpcServerActor` 채우기
- [ ] **CLI 앱 구현** — `CliApp` 실제 커맨드라인 인터페이스로 연결
- [ ] **HAL (Hardware Abstraction Layer)** — epoll 등 플랫폼 의존 코드 추상화
- [ ] **테스트 코드 작성** — CTest 기반 unit test suite

---

## 🔧 빌드

```bash
cmake -B build && cmake --build build
```

실행 파일:
- `build/src/app/demo/v2_demo` — 액터 시스템 데모 (4 workers + 1 actor, 100ms timer)
- `build/src/app/cli/v2_cli` — CLI 앱 (준비 중)

---

## 📦 의존성

- C++17 이상의 컴파일러 (GCC / Clang)
- Linux (epoll, timerfd, eventfd)
