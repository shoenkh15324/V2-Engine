# 인프라 계층 (포트와 어댑터)

epoll·소켓·timerfd 같은 운영체제 기능을 액터 코어에 연결하는 **인프라 계층**(`src/infra/`)을
처음 읽는 사람도 따라올 수 있게 정리한 문서.

---

## 목차

- [요약 (세 문장)](#요약-세-문장)
- [개요 — 포트와 어댑터](#개요--포트와-어댑터)
- [왜 분리하는가](#왜-분리하는가)
- [포트 — 코어가 정의한 인터페이스](#포트--코어가-정의한-인터페이스)
- [어댑터 카탈로그](#어댑터-카탈로그)
- [EventLoopEpoll — 심장 박동](#eventloopepoll--심장-박동)
- [타이머 — LinuxTimer vs Timer](#타이머--linuxtimer-vs-timer)
- [SignalHandler — 안전한 시그널 수신](#signalhandler--안전한-시그널-수신)
- [UdsServer/UdsClient — 프로세스 간 통로](#udsserverudsclient--프로세스-간-통로)
- [JsonConfigLoader — 설정 주입](#jsonconfigloader--설정-주입)
- [HAL — 하드웨어 추상화](#hal--하드웨어-추상화)
- [조립 — main_app에서의 배선](#조립--main_app에서의-배선)
- [계층 규칙](#계층-규칙)

---

## 요약 (세 문장)

1. 인프라 계층은 **시스템 콜과 서드파티 라이브러리를 만질 수 있는 유일한 곳**이며, 코어가 선언한 포트(`IEventLoop`, `ITimer` 등)를 어댑터로 구현합니다.
2. epoll 이벤트 루프, timerfd 타이머, self-pipe 시그널, 유닉스 도메인 소켓 같은 OS 연동이 전부 이 계층에 갇혀 있습니다.
3. 어떤 구현체를 꽂을지는 애플리케이션 계층의 조립(`main_app.cpp`)에서 결정되고 — 코어는 구현이 무엇인지 끝까지 모릅니다.

---

## 개요 — 포트와 어댑터

V² Engine은 **육각형 아키텍처**(헥사고널, 또는 포트-앤드-어댑터 패턴)를 따릅니다.
처음 들으면 어려워 보이지만 개념은 가전제품과 같습니다:

| 개념 | 비유 | V² Engine에서 |
|------|------|----------------|
| **포트(port)** | 벽의 **콘센트 규격** | 코어가 정의하는 순수 가상 인터페이스 (`IEventLoop`, `ITimer` 등) |
| **어댑터(adapter)** | 규격에 맞춘 **플러그** | 그 인터페이스를 실제로 구현한 클래스 (`EventLoopEpoll`, `LinuxTimer` 등) |

핵심은 **의존성 방향**입니다:

```
코어:  "나는 IEventLoop 모양의 콘센트만 알아. 뒤에 뭐가 꽂히는지 몰라."
인프라: "그 콘센트에 epoll 플러그를 꽂겠습니다."
```

코어(`src/core/`)는 `epoll_wait`라는 단어조차 모릅니다. 코어는 "이벤트 루프다운 행동"
(start/run/stop/post/subscribe)을 인터페이스로 선언할 뿐입니다. 리눅스 전용 지식은
전부 인프라에 갇혀 있고, 인프라가 코어의 인터페이스를 **구현**합니다. 화살표가 안쪽을 향하므로
이걸 **의존성 역전(dependency inversion)**이라 부릅니다.

---

## 왜 분리하는가

1. **테스트**: OS 없이도 코어 로직을 검증할 수 있습니다. 느린 실제 epoll 대신 가짜 이벤트 루프(Mock)를 꽂으면 밀리초 단위로 시간을 조작하는 테스트가 가능합니다.
2. **이식성**: 라즈베리파이(aarch64)에서는 진짜 PMU 드라이버, x86 개발 머신에서는 Mock을 꽂는 식으로 플랫폼별 차이를 한 곳에 모읍니다.
3. **명확한 경계**: "시스템 콜을 부를 수 있는 곳은 오직 인프라뿐"이라는 규칙 하나로 코어 전체가 순수 C++20(+스레드 라이브러리)으로 유지됩니다.

---

## 포트 — 코어가 정의한 인터페이스

코어 쪽에 있는 "콘센트" 목록입니다:

| 포트 | 파일 | 하는 일 |
|------|------|---------|
| `IEventLoop` | `src/core/actor_system/runtime/dispatcher/io/i_event_loop.hpp` | fd 감시 시작/정지, 작업 post, 핸들러 등록(subscribe/unsubscribe) |
| `ITimer` | `src/core/common/timer/i_timer.hpp` | 타이머 등록(add/cancel/clear), 만료 콜백 실행(handleTimerEvent) |
| `IMemoryAllocator` | `src/core/common/memory/i_memory_allocator.hpp` | 메모리 할당/해제 + 사용량 통계 |

```cpp
// IEventLoop — 코어가 아는 전부
class IEventLoop {
public:
    using Handler = std::function<void()>;
    virtual void start() = 0;
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual void post(std::function<void()> op) = 0;
    virtual int  subscribe(WatchedFd fd, Handler handler) = 0;
    virtual int  unsubscribe(WatchedFd fd) = 0;
};
```

서비스 계층의 액터들은 이 인터페이스 포인터만 받아서 사용합니다. "내가 받은 게 epoll인지,
테스트용 가짜인지" 전혀 모릅니다.

---

## 어댑터 카탈로그

`src/infra/`에 살아있는 "플러그" 목록입니다:

| 모듈 | 구현체 | 대응 포트/역할 |
|------|--------|----------------|
| 플랫폼 | `EventLoopEpoll` | `IEventLoop` — epoll 기반 이벤트 루프 |
| | `LinuxTimer` | `ITimer` — timerfd 기반 타이머 |
| | `Epoll` | epoll syscall 얇은 래퍼(add/mod/del/wait) |
| | `SignalHandler` | POSIX 시그널 → self-pipe 변환기 |
| 전송 | `UdsServer` / `UdsClient` | 유닉스 도메인 소켓 (IPC·TUI 피드용) |
| 설정 | `RuntimeConfig::loadFromFile` | nlohmann/json 설정 파싱 |
| HAL | `PmuRsp5` / `PmuMock` | `IPmu` — 전력관리 칩 데이터 |
| | `SysLinux` / `SysMock` | `ISys` — CPU/메모리 등 시스템 자원 |
| | `I2cLinux` | `II2c` — I2C 버스 접근 |
| UI | `v2_tui` 직접 사용 (FTXUI) | 터미널 렌더링 |
| 테스트 지원 | `MockAllocator`, `MockTimeSource` 등 | Mock 교체용 어댑터 |

> 참고: `infra/ui/ftxui_renderer.*`와 `infra/memory/memory_pool_allocator.*`는 현재
> 빈 플레이스홀더입니다. UI는 당분간 `v2_tui`가 FTXUI를 직접 사용합니다.

---

## EventLoopEpoll — 심장 박동

**파일:** `src/infra/platform/linux/event_loop_epoll.hpp`, `.cpp`

엔진의 모든 외부 이벤트(소켓 접속, 데이터 도착, 시그널, 타이머 만료)는 이 루프 하나로
흘러들어옵니다. **단일 스레드**(스레드 이름 `v2-main`)에서 돕니다.

### 루프 구조

```cpp
void EventLoopEpoll::run(){
    pthread_setname_np(pthread_self(), "v2-main");
    while(running_.load(std::memory_order_relaxed)){
        drainPendingOps();                    // 1. 다른 스레드가 맡겨놓은 작업 처리
        int n = epoll_.wait(..., waitTimeoutMs_); // 2. 이벤트 대기 (최대 1000ms)
        for(int i = 0; i < n; i++){           // 3. 이벤트마다 등록된 핸들러 호출
            ...
            handler();
        }
    }
}
```

1. **드레인**: 다른 스레드가 `post()`로 맡긴 함수들을 전부 실행
2. **대기**: `epoll_wait`로 준비된 fd가 생길 때까지 잔다 (타임아웃 1000ms — 하트비트 겸용)
3. **디스패치**: 이벤트가 온 fd에 등록된 `Handler`(std::function) 호출

핸들러 안에서 액터 메일박스로 메시지를 넣으면, 외부 세계와 액터 세계가 연결됩니다.

### 다른 스레드 깨우기 (wakeup) — eventfd

`epoll_wait`로 대기 중일 때 누군가 `post()`를 하면 어떻게 될까요? 여기서
**eventfd**가 씁니다. eventfd는 "wakeup 전용 미니 파이프" 같은 fd로, 이 루프에
미리 등록되어 있습니다:

```cpp
void EventLoopEpoll::post(std::function<void()> op){
    while(!pendingOps_.push(std::move(op))){
        std::this_thread::yield();     // 큐 가득 → 잠깐 양보 후 재시도
    }
    uint64_t one = 1;
    ::write(stopFd_, &one, sizeof(one));   // ★ eventfd에 쓰기 → epoll_wait 즉시 기상
}
```

작업은 락프리 MPSC 큐(`pendingOps_`, 용량 128)에 쌓고, eventfd 쓰기 한 번으로
잠자던 루프를 깨웁니다. `stop()`도 같은 원리입니다 — `running_=false` 후 eventfd에
써서 루프가 즉시 빠져나오게 합니다.

### 스레드 어피니티 감지 (thread affinity)

`subscribe()`는 **지금 이벤트 루프 스레드에서 불렸는지** 스스로 판단합니다:

```cpp
int EventLoopEpoll::subscribe(WatchedFd fd, Handler handler){
    bool isLoopThread = std::this_thread::get_id() == threadId_;
    { /* handlers_ 맵에 등록 (뮤텍스) */ }
    if(isLoopThread) return add();                     // 직접 실행
    post([add = ...]() mutable { add(); });            // 아니면 루프 스레드에 위임
    return Ok;
}
```

`epoll_ctl`은 같은 fd를 동시에 건드리면 위험하니, 워커 스레드에서 불렸다면
작업을 큐에 넣어 루프 스레드에서 실행시킵니다. 호출자는 신경 쓸 필요가 없습니다.

---

## 타이머 — LinuxTimer vs Timer

**파일:** `src/infra/platform/linux/timer_linux.hpp`, `src/core/common/timer/timer.hpp`

타이머에는 두 가지 구현이 있고, 공통 로직은 부모 클래스 `TimerBase`가 가집니다:

| 구현 | 위치 | 동작 방식 | 장점 |
|------|------|-----------|------|
| `Timer` (std 폴백) | 코어 | 자기 스레드 + 세마포어로 대기 | 어디서든 동작 (macOS/Windows 포함) |
| `LinuxTimer` (기본) | 인프라 | **timerfd**를 이벤트 루프에 등록 | 전용 스레드 불필요, 루프와 자연 통합 |

timerfd는 "타이머가 만료되면 읽을 수 있게 되는 특별한 파일"입니다. 따라서
`LinuxTimer`는 만료 시점을 별도 스레드 없이 **epoll 이벤트**로 받습니다.

공통 로직(`TimerBase`)은 만료 예정 타이머들을 **최소힙**으로 관리합니다:

```cpp
struct TimerNode {
    Clock::time_point expiry;      // 언제 울릴까
    std::chrono::milliseconds interval;
    Callback cb;
    bool repeating;                // 반복 타이머?
    bool alive;
};
```

`handleTimerEvent()`가 만료된 것들을 실행하고, 반복 타이머는 다음 만료를 다시 힙에 넣습니다.
액터의 `startTimer()`([Actor Model](actor_model.md))가 결국 이 장치를 통해 울립니다.

---

## SignalHandler — 안전한 시그널 수신

**파일:** `src/infra/platform/linux/signal_handler.hpp`, `.cpp`

Ctrl+C(SIGINT)나 종료 요청(SIGTERM) 같은 POSIX 시그널은 일반 코드처럼 다루면 안 됩니다.
시그널 핸들러는 **언제든 스레드를 가로채서 실행**되므로, 거기서 할 수 있는 일이
극도로 제한됩니다(로그 출력조차 위험).

해법은 유명한 **self-pipe 트릭**입니다:

```
1. pipe2(O_NONBLOCK | O_CLOEXEC)로 파이프를 만든다
2. 실제 시그널 핸들러는 딱 한 가지만 한다:
       ::write(pipe_[1], &sig, sizeof(sig))   ← async-signal-safe한 write
3. 파이프 읽기 끝(pipe_[0])을 이벤트 루프에 등록한다
4. 루프가 "파이프에 데이터 도착" 이벤트를 받으면
   dispatch(sig)로 일반 스레드 문맥에서 콜백들을 실행한다
```

즉 위험한 시그널 문맥에서는 "우편함에 편지 투척"만 하고, 실제 처리는 평화로운
이벤트 루프 스레드에서 합니다. `main_app.cpp`에서는 이렇게 연결됩니다:

```cpp
SystemManagerActor::onSignal(SIGINT,  [this](int){ requestStop(); });
SystemManagerActor::onSignal(SIGTERM, [this](int){ requestStop(); });
```

세부 사항: 콜백은 시그널 번호별 배열(`std::array<std::vector<Callback>, 65>`)에 저장되고,
`SIGKILL`/`SIGSTOP`은 커널이 무시를 허용하지 않으므로 설치를 거부합니다. write 실패 시
errno를 보존해 원래 문맥을 오염시키지 않습니다.

---

## UdsServer/UdsClient — 프로세스 간 통로

**파일:** `src/infra/transport/uds/uds_server.hpp`, `uds_client.hpp`

유닉스 도메인 소켓(UDS)은 **같은 머신 안의 프로세스끼리** 대화하는 소켓입니다.
TCP와 달리 네트워크 스택을 안 타서 빠르고, 파일 경로 기반 권한 제어가 가능합니다.

| 클래스 | 역할 | 사용자 |
|--------|------|--------|
| `UdsServer` | `start(path, backlog)`로 듣기 → `accept()`로 접속 수령 → `send/recv` | 데몬 쪽: `IpcServerActor`(CLI 명령), `MonitorBridgeActor`(TUI 피드) |
| `UdsClient` | `connect(path)`로 접속 → `send/recv` | 클라이언트 쪽: `v2_cli`, `v2_tui` |

```
v2_cli ──(connect /tmp/v2.sock)──► UdsServer(IpcServerActor)
v2_tui ──(connect monitor.sock)──► UdsServer(MonitorBridgeActor)
                                        │
                                   액터 메시지로 변환
```

두 클래스 모두 복사/이동 정책이 명확하고(`non-copyable, movable`), RAII로 fd를
닫습니다. 소켓 데이터가 도착하면 epoll이 깨워주고, 서비스 액터가 그 내용을
[Message](messaging.md)로 포장해 액터 세계로 가져옵니다.

---

## JsonConfigLoader — 설정 주입

**파일:** `src/infra/config/json_config_loader.cpp`

JSON 설정 파일을 읽어 `RuntimeConfig` 구조체를 채우는 짧고 단순한 어댑터입니다.

```cpp
RuntimeConfig cfg = RuntimeConfig::loadFromFile(V2_CONFIG_DIR "/v2_main.json");
if(j.contains("worker_count")) cfg.workerCount = j["worker_count"];
```

- 파일이 없거나 파싱에 실패해도 **예외 없이 기본값**으로 진행합니다(데몬이 설정 때문에 죽지 않게 함).
- 키가 없으면 해당 필드는 C++ 기본값을 유지합니다.
- 읽는 키의 전체 목록: 엔진(`worker_count`, `park_spin_ns` 등), 메일박스, 디스패처, 슈퍼바이저, 메모리(`memory_slab_size` 등), Tick/Monitor/IPC/D-Bus 활성화 여부 등 — [작업 분배](work_dispatch.md)·[Memory](memory.md)·[Supervision](supervision.md) 문서의 "설정" 절에서 각각 다룹니다.

플랫폼 전용 키(epoll, IPC, D-Bus)는 `#if V2_PLATFORM_LINUX`로 감싸여 있습니다.

---

## HAL — 하드웨어 추상화

HAL(Hardware Abstraction Layer)은 센서·하드웨어 데이터를 읽는 어댑터 묶음입니다.
목표 프로젝트가 라즈베리파이 계열이므로 실구현과 Mock이 나란히 있습니다:

### PMU (전력관리 칩)

```cpp
class IPmu{                       // 포트
    virtual int readPmuData(PmuData& data) = 0;
};

class PmuRsp5 : public IPmu {};   // 어댑터 — Raspberry Pi 5
```

`PmuRsp5`는 라즈베리파이5의 `vcgencmd` 유틸리티를 실행해 클럭/메모리/스로틀링/
온도/전압/전류를 파싱합니다. 개발 PC(x86)에서는 `PmuMock`이 바인딩되어
하드웨어 없이도 전체 파이프라인이 동작합니다.

### Sys (시스템 자원)

`SysLinux`는 procfs(`/proc`)를 읽어 CPU 사용률·메모리 등을 수집합니다.
CPU 사용률은 1초 윈도우의 샘플 두 개(`std::deque<CpuSample>`)로 계산하는
식입니다.

### I2C

`I2cLinux`는 `/dev/i2c-N` 문자 디바이스를 열어 저수준 read/write/transfer를 제공합니다.

### 선택은 조립 단계에서

어떤 구현을 쓸지는 `main_app.cpp:96`의 DI 컨테이너 바인딩이 결정합니다:

```cpp
#if V2_PLATFORM_LINUX && defined(__aarch64__)
    di_.bind<IPmu, PmuRsp5>(Lifetime::Singleton);   // 진짜 하드웨어
#else
    di_.bind<IPmu, PmuMock>(Lifetime::Singleton);   // 개발 환경
#endif
```

서비스 액터(`DeviceManagerActor` 등)는 `IPmu*`만 받으므로, 어느 쪽이 꽂혔는지
전혀 모른 채 동일하게 동작합니다. 이것이 포트/어댑터 분리의 실질적 이득입니다.

---

## 조립 — main_app에서의 배선

**파일:** `src/app/main/main_app.cpp`

모든 어댑터는 애플리케이션 계층의 조립 루트(composition root)에서 꽂힙니다:

```
MainApp::open()
  ├─ RuntimeConfig::loadFromFile()          ← 설정 어댑터
  ├─ initGlobalMemoryPoolConfig(...)        ← 메모리 풀 설정 ([Memory](memory.md))
  ├─ SystemManagerActor::onSignal(...)      ← 시그널 어댑터
  ├─ eventLoop_ = make_unique<EventLoopEpoll>(...)   ← IEventLoop 플러그
  ├─ timer_     = make_unique<LinuxTimer>(eventLoop_)  ← ITimer 플러그
  ├─ di_.bind<IPmu/PmuRsp5|PmuMock>() ...   ← HAL 플러그
  └─ actorSystem_ = createDefaultActorSystem(sysConfig, move(eventLoop_), move(timer_))
                                             ↑ 인프라 객체들을 코어에 주입!
```

마지막 줄이 핵심입니다. **코어(ActorSystem)는 생성자로 인프라 객체를 받습니다.**
코어 코드 어디에도 `new EventLoopEpoll` 같은 문장이 없습니다. 이 한 줄의 배선만
바꾸면 테스트에서 가짜 루프/타이머로 전체 시스템을 구동할 수 있습니다.

---

## 계층 규칙

인프라 작업 시 지키는 규칙:

| 규칙 | 이유 |
|------|------|
| 시스템 콜·서드파티 라이브러리는 **오직 인프라에서만** | 코어를 순수 C++20으로 유지, 테스트 가능성 확보 |
| 인프라는 코어의 포트 인터페이스를 **구현**한다 (참조하지 않은 새 의존성을 만들지 않는다) | 의존성 역전 유지 |
| 플랫폼 코드는 `V2_PLATFORM_*` 매크로로 감싼다 | macOS/Windows 빌드 호환 |
| 어댑터는 실패를 코드 반환값(int Ok/Fail)으로 보고 | 예외를 코어 경계 너머로 던지지 않음 |
| fd 소유는 RAII로 | 소멸자에서 확실히 close |
