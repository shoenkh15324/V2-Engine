<p align="center">
  <img src="https://img.shields.io/badge/c%2B%2B-17-blue.svg" alt="c++17">
  <img src="https://img.shields.io/badge/platform-linux-lightgrey.svg" alt="platform">
  <img src="https://img.shields.io/badge/version-0.4.0-orange.svg" alt="version">
</p>

<h1 align="center">V2 Engine</h1>
<p align="center">
  <b>경량 C++17 액터 모델 엔진</b><br>
</p>

---

## ✨ 한눈에

```cpp
ActorSystem sys;
sys.createActor<MyActor>("my_actor", cfg);
sys.send("my_actor", MyMessage{42});
```

> **공유 메모리 없이, 뮤텍스 없이 — 메시지 하나면 끝납니다.**

경량 액터 모델 위에 UDS IPC, D-Bus 게이트웨이, I2C 디바이스 매니저, 타이머 스케줄러, 실시간 TUI 모니터를 모두 갖춘 \
**C++17 풀스택 서비스 프레임워크**입니다.

---

## 🎬 데모

```
$ v2 info

  V2 Engine v0.4.0
  PID: 222320 | RSS: 3.8 MB | Uptime: 0h 14m 23s
  Actors: tick(Idle), monitor(Idle), ipc_srv(Idle), cmd(Idle), dbus(Idle)

$ v2 actor -l

  Name              ID  State    Essential  Mailbox
  tick               0  Idle     no         0/512
  monitor            1  Idle     yes        0/512
  ipc_srv            2  Idle     yes        0/512
  cmd                3  Idle     yes        0/512
  dbus               4  Idle     yes        0/512
  device_manager     5  Idle     yes        0/512
```

```
$ v2 -m
```

<img src="docs/tui_screenshot.png" alt="V2 Engine TUI Screenshot" width="720">

---

## 🚀 빠르게 시작

### 직접 빌드

```bash
cmake -B build && cmake --build build
./build/v2_main &            # 데몬 실행
./build/v2_cli info          # 정보 확인

./build/v2_cli actor -l      # 액터 목록
./build/v2_cli monitor       # TUI 모니터 실행
```

### 설치 스크립트 (systemd + D-Bus)

```bash
./install.sh              # 의존성 설치 → 빌드 → systemd 등록 → D-Bus 정책 → symlink
./install.sh v2_main      # 특정 앱만 설치
SKIP_DEPS=1 ./install.sh  # 의존성 설치 생략
./uninstall.sh            # 제거
```

설치 후 `v2` 명령어로 CLI 사용 가능 (`/usr/local/bin/v2`).

---

## 🔧 기능

- **Actor Model** — 경량 액터 + bounded mailbox, 협력적 스케줄링 (epoll + semaphore)
- **std::variant 메시지** — `std::visit` 기반, 상속/형변환 제로
- **epoll Event Loop** — timer FD, stop FD, transport I/O 통합
- **Unix Domain Socket** — UDS 멀티클라이언트 IPC (서버/클라이언트)
- **Timer/Scheduler** — `timerfd_create()` + priority queue, one-shot/repeating
- **CLI + TUI** — 컬러 출력 CLI + FTXui 실시간 모니터
- **MonitorActor** — `/proc` 수집 (CPU/RSS/VmPeak/threads/load avg/sys mem), 스냅샷 프로토콜
- **CmdActor** — 메시지 기반 명령 라우팅 (`info`, `actor`, `test`), 액터 enable/disable
- **D-Bus Bridge** — 서버 측 메서드 등록 + 클라이언트 측 proxy 호출 및 시그널 구독
- **Device Manager** — 하드웨어 디바이스 등록/해제/열거, HAL 추상화 (I2C)
- **I2C HAL** — Linux `/dev/i2c-N` + `ioctl(I2C_RDWR)` 드라이버
- **Actor Lifecycle** — 상태 관리 (Closed/Opening/Opened/Closing), essential 플래그
- **Systemd Daemon** — `Type=simple`, 부팅 시 자동 실행
- **Config File** — JSON 기반 런타임 설정 (`config/*.json`)

---

## 📁 구조

```
src/
├── app/              # 실행파일
│   ├── main/         #   v2_main 데몬
│   ├── cli/          #   v2_cli CLI
│   └── tui/          #   v2_tui  TUI 모니터
├── service/          # 비즈니스 액터
│   ├── cmd/          #   명령 라우팅
│   ├── dbus/         #   D-Bus 서버/클라이언트
│   ├── device_manager/ # 하드웨어 관리
│   ├── ipc/          #   UDS IPC 서버
│   ├── monitor/      #   시스템 모니터링
│   └── tick/         #   주기적 틱
├── core/             # 액터 시스템 + 공통 유틸
│   ├── actor_system/ #   액터 런타임 (registry, mailbox, scheduler, worker)
│   └── common/       #   config, log, epoll, timer, ring_buffer
└── infra/            # 전송 계층 + HAL
    ├── transport/    #   UDS 서버/클라이언트
    └── hal/          #   I2C (Linux)
```

의존성은 단방향: `infra ← core ← service ← app`

---

## ⚙️ 설정

`config/v2_main.json` — 데몬 설정 예시
```json
{
    "log_level": 0,
    "worker_count": 1,
    "enable_tick": true,
    "enable_monitor": true,
    "enable_ipc_server": true,
    "enable_dbus": true,
    ...
}
```

---

## ⚙️ 개발 환경

### 필수

* C++17 지원 컴파일러 (GCC 8+ / Clang 7+)
* CMake 3.10 이상
* Linux 환경 (`epoll`, `timerfd`, `eventfd`)
* `lld` 링커
* `libsdbus-c++-dev` (시스템 패키지)

### 외부 라이브러리 (FetchContent 자동 다운로드)

* FTXUI v7.0.0 — TUI 프레임워크
* nlohmann/json v3.12.0 — JSON 파싱
