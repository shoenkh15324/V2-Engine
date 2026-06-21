# 🚀 V2 Engine (Visionary Vision Engine)

> 경량화된 C++17 액터 모델 엔진 — **고성능 동시성 서비스** 구축을 목표로 합니다.
>
> **핵심 목표:** 전통적인 lock 기반 동시성에서 벗어나, **액터 모델 + 이벤트 기반 아키텍처**를 통해
> 확장 가능하고 예측 가능한 멀티스레드 애플리케이션을 개발할 수 있는 엔진을 만드는 것.

---

## 🧱 아키텍처 개요

```
┌─────────────────────────────────────────────┐
│  src/app/      Application (main, cli)      │
├─────────────────────────────────────────────┤
│  src/service/  Business Services (IPC,Tick) │
├────────────────┬────────────────────────────┤
│  src/core/     │  src/infra/                │
│ Actor System   │  Transport · HAL           │
│ Common Utils   │  (UDS, epoll wrapper)      │
└────────────────┴────────────────────────────┘
```

3개의 **정적 라이브러리** 레이어로 구성되며, 의존성은 **단방향**으로 흐릅니다.
- `v2_core` ← `v2_service`
- `v2_infra` ← `v2_service` ← `v2_main` / `v2_cli`

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
| **IPC Server Actor** | UDS 기반 멀티클라이언트 명령어 처리, key:value 응답 프로토콜 |
| **CLI Tool** | 컬러 출력, 로컬/서버 명령어 분리, 자동 uptime 포매팅 |
| **Systemd Daemon** | `Type=simple` 서비스 유닛, `Restart=always` |
| **RAII / Move Semantics** | 복사 금지, 명시적 move only — 자원 소유권 명확 |
| **유틸리티** | Logging (ANSI color), RingBuffer, Semaphore, Time helpers, Assert/Panic 매크로 |

---

## 🔧 빌드

```bash
cmake -B build && cmake --build build
```

실행 파일:
- `build/v2_main` — 메인 데몬 (ActorSystem + IpcServerActor + TickActor)
- `build/v2_cli` — CLI 컨트롤 도구

---

## 📦 설치

```bash
./install.sh                    # 빌드 + systemd 등록 + /usr/local/bin/v2 symlink
```

- `v2_main`이 systemd 서비스로 등록되어 부팅 시 자동 실행됩니다.
- `v2` 명령어로 CLI 도구를 사용할 수 있습니다 (`/usr/local/bin/v2` → `build/v2_cli`).

제거:

```bash
./uninstall.sh
```

---

## 💻 사용법

```bash
Usage: v2 <command>

Commands:
  info      Show engine information

Options:
  version   Show version information
  help      Show this help message
  status    Show daemon status
```

### 예시

```bash
# 데몬 상태 확인 (서버 연결 불필요)
$ v2 status
  V2 Engine — Status
    Daemon: running
    Socket: /tmp/v2_ipc.sock
    PID:    222320

# 엔진 정보 조회
$ v2 info
  ▸ V2 Engine 0.0.1
    uptime: 0d 00h 14m 23s
    clients: 1

# 버전 확인
$ v2 version
  V2 Engine 0.0.1
```

---

## 🗺️ 프로젝트 구조

```
src/
├── app/
│   ├── main/          # v2_main: 데몬 엔트리포인트
│   │   ├── main.cpp
│   │   └── main_app.cpp / .hpp
│   └── cli/           # v2_cli: CLI 도구
│       ├── main.cpp
│       └── cli_app.cpp / .hpp
├── service/
│   ├── ipc/           # IpcServerActor
│   └── tick/          # TickActor
├── core/
│   ├── actor_system/  # Actor, ActorSystem, Dispatcher, Worker
│   └── common/        # Log, Time, Timer, RingBuffer, Config, Epoll
└── infra/
    └── transport/
        └── uds/        # UdsServer, UdsClient
```

---

## 📦 의존성

- C++17 이상의 컴파일러 (GCC / Clang)
- Linux (epoll, timerfd, eventfd, UDS)
