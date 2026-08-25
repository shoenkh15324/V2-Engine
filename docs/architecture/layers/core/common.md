# 코어 공통 유틸리티 (`src/core/common/`)

코어 계층 곳곳에서 쓰이는 공통 도구들 — 로깅, 시간, DI 컨테이너, 설정,
오류 보고, 링 버퍼 — 을 처음 읽는 사람도 따라올 수 있게 정리한 문서.

> 상위 구조는 [README.md](README.md)에서, 심층 메커니즘(큐·메모리·액터)은
> [concepts 문서](../../concepts/README.md)에서 다룹니다. 이 문서는 그 사이의
> "일상 도구"들을 다룹니다.

---

## 목차

- [개요](#개요)
- [로깅 — Logger](#로깅--logger)
  - [사용법 — V2_LOG 매크로](#사용법--v2_log-매크로)
  - [글로벌 로거 교체 패턴](#글로벌-로거-교체-패턴)
  - [버퍼링 — 왜 바로 출력하지 않나](#버퍼링--왜-바로-출력하지-않나)
- [시간 — Time과 Sleep](#시간--time과-sleep)
- [DI 컨테이너 — ServiceContainer](#di-컨테이너--servicecontainer)
- [설정 — platform_config와 RuntimeConfig](#설정--platform_config와-runtimeconfig)
- [오류 보고 — Result와 V2_ASSERT](#오류-보고--result와-v2_assert)
  - [Result 반환 코드](#result-반환-코드)
  - [V2_ASSERT / V2_PANIC / V2_UNREACHABLE](#v2_assert--v2_panic--v2_unreachable)
- [RingBuffer — 바이트 원형 버퍼](#ringbuffer--바이트-원형-버퍼)
- [요약](#요약)

---

## 개요

`src/core/common/`은 액터 프레임워크 본체(`actor_system/`)가 매일 사용하는
저수준 도구 상자입니다. 특징은 두 가지입니다:

1. **전부 코어 안에 자급자족** — 외부 라이브러리 없이 C++20 표준만 사용합니다.
2. **전역 상태는 "교체 가능한 포인터" 하나뿐** — 활성 로거(`gLogger`)와 활성
   메트릭스([metrics.md](metrics.md))만 글로벌이고, 나머지는 값으로 전달됩니다.

---

## 로깅 — Logger

**파일:** `src/core/common/log/log.hpp`, `.cpp`

### 사용법 — V2_LOG 매크로

로그를 남기는 유일한 방식은 매크로입니다:

```cpp
V2_LOG_INFO("Actor {} opened", name_.c_str());
// 출력 예: [2026-08-25T10:12:33] [INFO] actor.cpp:42 (open) Actor cmd opened
```

| 매크로 | 용도 | 비고 |
|--------|------|------|
| `V2_LOG_VERBOSE` | 추적용 잡담 | ANSI 회색 |
| `V2_LOG_DEBUG` | 개발 중 진단 | 흰색 |
| `V2_LOG_INFO` | 주요 이벤트 | 청록색 |
| `V2_LOG_WARN` | 복구 가능한 이상 | 노란색 |
| `V2_LOG_ERROR` | 실패했지만 계속 동작 | 빨간색 |
| `V2_LOG_FATAL` | 치명적 오류 | 밝은 빨강 + **즉시 플러시** |

포맷은 C++20 `std::format` 기반(`"{}"` 플레이스홀더)이고, 파일·줄번호·함수명은
매크로가 자동으로 붙입니다. 레벨별 ANSI 색상이 들어가 터미널에서 즉시 구분됩니다.

### 글로벌 로거 교체 패턴

```cpp
Logger& activeLogger();              // 현재 활성 로거 (없으면 fallback)
void setActiveLogger(Logger*);       // 조립 루트가 기동 초기에 1회 설정
```

코드 전체는 `activeLogger()`만 부르고, 실제 어떤 Logger 인스턴스가 응답할지는
조립 단계에서 결정합니다. 아무도 설정하지 않으면 내장 fallback 인스턴스가
동작하므로 **로거 미설정으로 프로그램이 죽는 일은 없습니다.**
실제 설정 위치는 `main_app.cpp:78`의 `setActiveLogger(&logger_)`입니다.

레벨 제어:

```cpp
logger.setLevel(LogLevel::Warn);     // Warn 미만은 즉시 폐기 (relaxed 원자 변수)
```

### 버퍼링 — 왜 바로 출력하지 않나

로그 한 줄을 파일/stderr에 쓰는 건 생각보다 비쌉니다(시스템 콜 + 잠금).
그래서 Logger는 이렇게 동작합니다:

```
log() 호출
  → thread_local 버퍼(gBuf)에 문자열 누적        ← 잠금 없음!
  → 버퍼가 flushBufferSize(기본 512B) 넘칠 때만
      flushBuffer(): 뮤텍스 획득 → stderr + 파일에 한 번에 출력 → 버퍼 비움
```

- **thread_local 버퍼**: 스레드별로 따로 모으므로 누적 단계에는 경쟁이 없습니다.
- **배치 플러시**: 여러 줄을 한 번의 write로 몰아씁니다.
- **`flushBuffer()`**: 수동 플러시. `V2_LOG_FATAL`과 소멸자는 버려질 로그가
  없도록 즉시 플러시를 강제합니다.
- **`logBlock(text)`**: assert 리포트처럼 "반드시 지금, 통째로" 출력해야 할 때
  쓰는 우회로입니다.

파일 출력 추가:

```cpp
logger.setLogFile("log/v2_main.log");   // stderr + 파일 이중 출력
```

---

## 시간 — Time과 Sleep

**파일:** `src/core/common/time/time.hpp`, `sleep.hpp`

두 종류의 시계를 목적에 맞게 나눠 씁니다:

| 클럭 | 용도 | 메서드 |
|------|------|--------|
| `steady_clock` (MonoClock) | 지속 시간 측정, 타이머 만료 계산 | `now()`, `nowMs()`, `nowUs()`, `nowNs()` |
| `system_clock` (SysClock) | 사람이 읽는 날짜/타임스탬프 | `nowEpoch()`, `nowDate()`, `nowDateString()` |

> ⚠️ 측정에는 반드시 steady_clock 계열을 씁니다(NTP 보정 등으로 갑자기 뒤로
> 가지 않음). system_clock은 로그 접두어처럼 "보여주기" 전용입니다.

단위 변환 헬퍼도 제공해 코드에서 raw 숫자가 다니지 않게 합니다:

```cpp
auto deadline = Time::afterUs(200);   // 지금부터 200μs 뒤 time_point
int64_t ns = Time::toNs(d);           // duration → 나노초 정수
auto d = Time::ns(3000);              // 3000ns → duration
```

`Sleep`은 이름 그대로의 얇은 래퍼입니다: `Sleep::sleepMs(ms)` / `sleepUs(us)` / `sleepSec(sec)`.

---

## DI 컨테이너 — ServiceContainer

**파일:** `src/core/common/di/service_container.hpp`

의존성 주입(Dependency Injection)을 위한 초경량 컨테이너입니다.
"구현체를 직접 new 하지 말고, 인터페이스 타입으로 등록하고 꺼내 쓰자"는 도구입니다.

```cpp
ServiceContainer di_;

// 등록 — IPmu 포트에 PmuRsp5 구현체를 싱글턴으로
di_.bind<IPmu, PmuRsp5>(Lifetime::Singleton);

// 조회 — 포트 타입으로 꺼냄 (PmuRsp5라는 이름은 여기 없음)
std::shared_ptr<IPmu> pmu = di_.resolve<IPmu>();
pmu->open();
```

| API | 설명 |
|-----|------|
| `bind<T, Impl>(lifetime)` | 기본 생성 가능한 구현체 등록 |
| `bindFactory<T>(팩토리)` | 생성자에 의존성이 필요한 경우 팩토리 함수로 등록 |
| `resolve<T>()` | 등록된 팩토리 실행 → shared_ptr 반환. 미등록이면 예외 |
| `contains<T>()` / `remove<T>()` | 확인/제거 |

생명주기(Lifetime):

- **Transient** — `resolve()`할 때마다 새 객체
- **Singleton** — 최초 1회 생성 후 공유

컴파일 타임 검증이 내장되어 있습니다:

```cpp
static_assert(std::is_base_of_v<T, Impl>, ...);   // Impl은 T를 상속해야 함
static_assert(!std::is_abstract_v<Impl>, ...);    // 추상 클래스 등록 불가
static_assert(std::is_default_constructible_v<Impl>, ...);
```

실제 활용 예는 [인프라 문서 §HAL](../../concepts/infrastructure.md)의
"aarch64면 `PmuRsp5`, 아니면 `PmuMock`" 선택 로직입니다.

---

## 설정 — platform_config와 RuntimeConfig

**파일:** `src/core/common/config/platform_config.h`, `runtime_config.h`

### platform_config.h — 컴파일 환경 감지

빌드 시점에 플랫폼·컴파일러를 매크로로 확정합니다:

```cpp
#if defined(__linux__)
    #define V2_PLATFORM_LINUX 1
#elif defined(_WIN32)
    #define V2_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #define V2_PLATFORM_MACOS 1
#endif
```

플랫폼별 차이가 필요한 코드는 이 매크로로 감쌉니다. 대표적인 타입 추상화가
연결 핸들입니다:

```cpp
#if V2_PLATFORM_WINDOWS
    using ConnHandle = uintptr_t;   // Windows SOCKET
#else
    using ConnHandle = int;         // POSIX fd
#endif
```

메시지 구조체들이 fd를 `ConnHandle`로만 다루므로, Windows 포팅 시 메시지
카탈로그([messaging.md](../../concepts/messaging.md))를 고칠 필요가 없습니다.

### runtime_config.h — 런타임 파라미터 묶음

```cpp
struct RuntimeConfig{
    // Engine: logLevel, workerCount, workerMaxBatch, park_spin_ns...
    // Mailbox: defaultMailboxSize
    // Dispatcher: queueCapacity, highWatermark
    // Supervisor: maxRestarts, defaultStrategy, deadLetterCapacity
    // Memory: slabSize, maxPoolAllocSize...
};
```

모든 필드에는 **합리적 기본값**(예: workerCount=4, maxRestarts=5)이 들어 있습니다.
JSON 파일에서 읽어 채우는 것은 [인프라의 JsonConfigLoader](../../concepts/infrastructure.md)가
담당하며, 파일이 없거나 일부 키만 있어도 나머지는 기본값으로 안전하게 진행합니다.

---

## 오류 보고 — Result와 V2_ASSERT

**파일:** `src/core/common/util/return.hpp`, `debug.hpp`

오류 처리 방식이 두 가지로 명확히 나뉩니다.

### Result 반환 코드

**복구 가능한 실패**는 예외가 아니라 정수 코드를 반환합니다:

```cpp
enum Result{
    Ok          =  0,   // 성공
    Fail        = -1,   // 범용 실패 (예: 큐 가득 참)
    InvalidArg  = -2,   // nullptr, 크기 0 등 잘못된 인자
    InvalidState= -3,   // 현재 상태에서 불가능한 연산
    Timeout     = -4,   // 시간 초과
};
```

관례: 성공은 `Ok == 0`, 실패는 음수. 호출부는 `if(ret != Ok)`로 확인합니다.
특히 [인프라 어댑터](README.md) 경계(IEventLoop 구현체 등)에서 예외를
던지지 않고 이 코드로 보고하는 것이 계약입니다.

### V2_ASSERT / V2_PANIC / V2_UNREACHABLE

**복구 불가능한 프로그래머 오류**(불변식 위반)는 즉시 멈춥니다:

```cpp
V2_ASSERT(epoll_.fd() >= 0, "EventLoopEpoll needs a valid epoll fd");
```

실패하면 FATAL 로그 + 상세 리포트 박스(표현식/파일/줄/함수)를 출력하고
디버거 브레이크포인트(`__builtin_trap`) 후 `abort()`합니다.

| 매크로 | 릴리즈 빌드에서 |
|--------|------------------|
| `V2_ASSERT(x, msg)` | **완전히 제거됨** (`NDEBUG`) — 성능 저하 없음 |
| `V2_PANIC(msg)` | 유지 — 논리적으로 도달 불가능한 지점 표시 |
| `V2_UNREACHABLE()` | `V2_PANIC()`의 별칭 |

철학: **사용자 입력 오류는 Result로, 우리 코드의 버그는 ASSERT로.**
후자는 "조용히 이상한 상태로 계속 도는" 것보다 요란하게 죽는 게 안전합니다.

---

## RingBuffer — 바이트 원형 버퍼

**파일:** `src/core/common/container/ring_buffer.hpp`, `.cpp`

고정 크기 바이트 배열을 끝에 닿으면 처음으로 감아 재사용하는 FIFO 버퍼입니다.
소켓 데이터 조각화 흡수 같은 바이트 스트림 처리용입니다.

```cpp
RingBuffer rb(4096);
rb.push(data, len);       // Ok / Fail(공간 부족) / InvalidArg
rb.pop(out, wantLen);     // Ok / Fail(데이터 부족) / InvalidArg
rb.freeSpace(); rb.count(); rb.reset();
```

내부 동작 — wrap-around 분할 복사:

```
[■■■■■□□□□□]  head가 배열 끝에 걸리면
push 4B:
[■■■■■■■■□□]  ← 앞 조각은 끝까지, 남은 조각은 buffer_[0]부터
                std::memcpy 두 번으로 자연스럽게 이어 붙임
```

주의점:

- **단일 스레드 전용**입니다. head/tail이 평범한 `size_t`(원자 변수 아님)라서
  멀티 스레드에서 쓰려면 별도 잠금이 필요합니다. 락프리 MPSC/MPMC 큐
  ([동시성 문서](../../concepts/concurrency.md))와 역할이 다릅니다 —
  이쪽은 *객체(Message)*, RingBuffer는 *날 바이트*용입니다.
- 실패 시 데이터 손실 없이 `Fail`을 반환하는 백프레셔 스타일입니다.

---

## 요약

| 구성요소 | 핵심 설계 | 주의점 |
|----------|-----------|--------|
| **Logger** | thread_local 배치 버퍼 + 글로벌 교체 패턴, `std::format` 매크로 | FATAL/logBlock만 즉시 출력 |
| **Time/Sleep** | 측정=steady_clock, 표시=system_clock으로 분리 | 측정에 system_clock 금지 |
| **ServiceContainer** | bind/resolve, Lifetime 2종, static_assert 검증 | 미등록 resolve는 예외 |
| **Config** | 플랫폼 매크로 감지, 기본값 완비된 RuntimeConfig | JSON 해석은 인프라 담당 |
| **Result** | 복구 가능 = 코드 반환 (Ok=0, 실패=음수) | 예외 대신 관례 준수 |
| **V2_ASSERT** | 불변식 위반 시 즉시 abort; 릴리즈에선 제거 | 사용자 입력에는 부적합 |
| **RingBuffer** | 분할 memcpy wrap-around, 단일 스레드 전용 | 멀티스레드엔 락프리 큐 사용 |
