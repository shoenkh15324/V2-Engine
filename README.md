<p align="center">
  <img src="https://img.shields.io/badge/c%2B%2B-17-blue.svg" alt="c++17">
  <img src="https://img.shields.io/badge/license-MIT-green.svg" alt="license">
  <img src="https://img.shields.io/badge/platform-linux-lightgrey.svg" alt="platform">
</p>

<h1 align="center">V2 Engine</h1>
<p align="center">
  <b>경량 C++17 액터 모델 엔진</b><br>
  lock 대신 메시지로 동시성을 다루는 방법
</p>

---

## ✨ 한눈에

```
// 액터를 만들고 메시지를 보내는 것만으로 동시성 완료
actorSystem.send("my_actor", MyMessage{42});
```

V2 엔진은 액터 모델 위에 UDS IPC, 타이머 스케줄러, CLI/TUI 도구를 갖춘 풀스택 서비스 프레임워크입니다.

---

## 🎬 데모

```
$ v2 monitor

┌─────────────────┬────────┬───────────────────┬─────────┬─────────────┐
│ ■ V2 Engine TUI │ v0.0.1 │ 0h 14m 23s        │ 3 actors │ 1 client   │
└─────────────────┴────────┴───────────────────┴─────────┴─────────────┘
┌ Actors (3) ────┬┬──────────┬───────╮┌ Process Info ────────────────╮
│  0  │ tick     ││0/512 (...│ ○Idle ││ PID: 222320                 │
│  1  │ monitor  ││0/512 (...│ ○Idle ││ RSS: 3.8 MB                │
│  2  │ ipc_srv  ││0/512 (...│ ○Idle ││ ...                        │
└────────────────┴┴──────────┴───────╯└─────────────────────────────╯
                                        ┌ System Resources ─┬─────────┐
                                        │ CPU  │□□□□□□□□□□□│  0.4%   │
                                        │ MEM  │□□□□□□□□□□□│  3.8 MB │
                                        │ SYS  │□□□□□□□□□□□│  0.0 MB │
                                        │ LOAD │□□□□□□□□□□□│  0.0/0.0│
                                        └──────┴────────────┴─────────┘
```

---

## 🚀 빠르게 시작

```bash
cmake -B build && cmake --build build
./build/v2_main &           # 데몬 실행
./build/v2_cli info          # 정보 확인
./build/v2_cli monitor       # TUI 모니터
```

---

## 🔧 기능

- **Actor Model** — 경량 액터 + bounded mailbox, 협력적 스케줄링
- **std::variant 메시지** — `std::visit` 기반, 상속/형변환 제로
- **epoll Event Loop** — timer FD, stop FD, transport I/O 통합
- **Unix Domain Socket** — UDS 멀티클라이언트 IPC
- **Timer/Scheduler** — `timerfd_create()` + priority queue
- **CLI + TUI** — 컬러 출력 CLI + FTXui 실시간 모니터
- **MonitorActor** — `/proc` 수집 (CPU/RSS/VmPeak/threads/load avg/sys mem)
- **Systemd Daemon** — `Type=simple`, 부팅 시 자동 실행

---

## 📁 구조

```
src/
├── app/           # 실행파일 (main / cli / tui)
├── service/       # 비즈니스 액터 (ipc / monitor / tick)
├── core/          # 액터 시스템 + 공통 유틸
└── infra/         # 전송 계층 (UDS)
```

의존성은 단방향: `core ← service ← app`, `infra ← service`

---

## 📦 설치

```bash
./install.sh       # 빌드 + systemd 등록
./uninstall.sh     # 제거
```

---

## ⚙️ 개발 환경

### 필수

* C++17 지원 컴파일러 (GCC / Clang)
* CMake 3.10 이상
* Ninja Build
* Linux 환경 (`epoll`, `timerfd`, `eventfd` 사용)
* `lld` 링커 (Linux)

### 외부 라이브러리

다음 라이브러리는 **CMake FetchContent**를 통해 자동으로 다운로드되므로 별도의 설치가 필요하지 않습니다.

* FTXUI (v7.0.0)
* nlohmann/json (v3.12.0)
