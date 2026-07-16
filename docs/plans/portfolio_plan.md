# V2-Engine 포트폴리오 업그레이드 계획

> **목적**: 런타임 시스템 / 소프트웨어 아키텍처 석사 지원을 위한 대학원 포트폴리오
> **목표**: 연구실 연락용 포트폴리오 + 학술 수준 성능 분석 보고서
> **일정**: 3개월 이상
> **언어**: 영어 문서

---

## 목차

1. [프로젝트 개요](#1-프로젝트-개요)
2. [현재 상태 분석](#2-현재-상태-분석)
3. [업그레이드 목표](#3-업그레이드-목표)
4. [단계 요약](#4-단계-요약)
5. [기술 아키텍처](#5-기술-아키텍처)
6. [핵심 설계 결정](#6-핵심-설계-결정)
7. [산출물](#7-산출물)
8. [위험 평가](#8-위험-평가)
9. [참고 자료](#9-참고-자료)

---

## 1. 프로젝트 개요

### 1.1 V2-Engine란?

V2-Engine은 임베디드 Linux 시스템을 위한 경량 C++20 액터 모델 기반 서비스 프레임워크입니다. 다음을 제공합니다:

- **액터 시스템**: `std::variant` 기반 메시지로 통신하는 메시지 기반 액터
- **Epoll 기반 디스패처**: 세마포어 기반 작업 디스패치가 있는 비동기 이벤트 루프
- **서비스 액터**: IPC (Unix Domain Socket), D-Bus 통합, 시스템 모니터링, WiFi 관리
- **실시간 TUI 모니터**: FTXUI로 구현된 실시간 시스템 시각화
- **CLI 도구**: 시스템 제어 및 검사를 위한 명령줄 인터페이스
- **메트릭 및 벤치마크**: 성능 측정 인프라

### 1.2 대학원 지원에 이 프로젝트를 선택한 이유?

| 측면 | 런타임 시스템과의 관련성 |
|--------|------------------------------|
| 액터 모델 구현 | Erlang/OTP, Akka, Tokio에서 사용되는 핵심 동시성 패턴 |
| 잠금 해제 데이터 구조 | MPSC 메일박스 설계는 메모리 순서에 대한 깊은 이해 필요 |
| 세마포어 기반 스케줄링 | Go의 고루틴 스케줄러와 비교되는 작업 디스패치 메커니즘 |
| Epoll 통합 | Node.js/libuv 이벤트 루프와 유사한 Linux I/O 멀티플렉싱 |
| 성능 측정 | 경험적 평가 방법론, 벤치마크 모범 사례 |
| 임베디드 Linux 대상 | 실제 제약 조건: 메모리, 지연 시간, 리소스 관리 |

### 1.3 대상 독자

- 연구 역량을 평가하는 **대학원 교수**
- 런타임 시스템 연구에 대한 기술 적합성을 평가하는 **연구실 구성원**
- 시스템 프로그래밍 역량을 찾는 **면접관**

---

## 2. 현재 상태 분석

### 2.1 구현된 기능

| 카테고리 | 기능 | 상태 |
|----------|---------|--------|
| **코어 런타임** | 생명주기 상태를 가진 액터 기본 클래스 | 완료 |
| | ActorContext (메일박스 + 스케줄링 게이트) | 완료 |
| | ActorRegistry (이름/ID 조회) | 완료 |
| | ActorSystem (오케스트레이터) | 완료 |
| **디스패처** | Epoll 기반 이벤트 루프 | 완료 |
| | 세마포어 기반 작업 디스패치 | 완료 |
| | 중복 제거가 있는 FIFO 준비 큐 | 완료 |
| **워커** | 세마포어 블로킹이 있는 스레드 풀 | 완료 |
| | 설정 가능한 배치 처리 (maxBatch) | 완료 |
| **메일박스** | 뮤텍스 보호 바운드 링 버퍼 | 완료 |
| **스케줄러** | timerfd 기반 타이머 스케줄링 | 완료 |
| | 일회성 및 반복 타이머 | 완료 |
| **메시지** | `std::variant` 기반 메시지 타입 (25개 이상) | 완료 |
| **서비스** | IPC 서버 (UDS) | 완료 |
| | D-Bus 게이트웨이 (sdbus-c++) | 완료 |
| | 시스템 모니터 (procfs) | 완료 |
| | WiFi 관리자 (NetworkManager D-Bus) | 완료 |
| | 명령 라우터 | 완료 |
| | 틱 생성기 | 완료 |
| **인프라** | Epoll 래퍼 | 완료 |
| | 시그널 핸들러 | 완료 |
| | 타이머 (timerfd + 우선순위 큐) | 완료 |
| | RingBuffer (바이트 지향, 단일 스레드) | 완료 |
| | JSON 설정 시스템 | 완료 |
| **메트릭** | 액터, 워커, 디스패처 메트릭 | 완료 |
| | 활성화/비활성화 토글 | 완료 |
| | 스냅샷 API | 완료 |
| **테스트** | 10개 단위 테스트 파일 (GoogleTest) | 완료 |
| | 2개 통합 테스트 | 완료 |
| | 4개 Google Benchmark 스위트 | 완료 |
| **CLI** | 명령줄 인터페이스 | 완료 |
| | TUI 실시간 모니터 | 완료 |

### 2.2 빈 스캐폴드 (구현 예정)

| 파일 | 용도 |
|------|---------|
| `bench_latency.cpp` | 메시지당 지연 시간 측정 |
| `bench_contention.cpp` | 고등쟁용 성능 테스트 |
| `bench_scaling.cpp` | 워커/액터 스케일링 분석 |
| `bench_backpressure.cpp` | 메일박스 오버플로우 동작 |
| `bench_scheduler.cpp` | 타이머 스케줄링 오버헤드 |
| `tcp/` 디렉토리 | TCP 전송 (플레이스홀더) |

### 2.3 발견된 병목 현상

| 병목 현상 | 위치 | 영향 | 우선순위 |
|------------|----------|--------|----------|
| **메일박스 뮤텍스 경쟁** | `mailbox.hpp` push/pop/empty/count | 주요_throughput 제한 요인 | **치명적** |
| **인큐당 이중 뮤텍스 획득** | `actor_context.cpp:26` (push() 후 count()) | 불필요한 오버헤드 | **높음** |
| **배치 루프당 메시지별 뮤텍스** | `actor_context.cpp:37` (while 루프의 pop()) | maxBatch에 비례 | **높음** |
| **64 워커 > 16 액터** | 벤치마크 설정 불일치 | 과도한 세마포어 경쟁 | **중간** |
| **핫 패스의 Time::now()** | `actor_context.cpp:33,41` + `worker.cpp` | 100K 반복당 ~12ms 오버헤드 | **낮음** |
| **작업 도둑질 없음** | 워커 설계 | 로드 불균형 가능 | **낮음** |

### 2.4 현재 벤치마크 결과

```
벤치마크: throughput
워커: 64, 액터: 16, MaxBatch: 128, 반복: 100,000

처리량: 4874.27 메시지/초
지연 시간:    205159.01 ns/메시지
총 시간: 20515.9 ms
```

**분석**: 64 워커에서의 4874 메시지/초 처리량은 뮤텍스 경쟁과 워커/액터 비율 불균형으로 인해 비최적입니다. 잠금 해제 메일박스 구현이 이를 크게 개선할 것으로 예상됩니다.

---

## 3. 업그레이드 목표

### 3.1 주요 목표

1. **벤치마크 프레임워크 완성** - 모든 빈 벤치마크 스캐폴드 구현 및 메일박스 비교 벤치마크 추가
2. **시스템 핵심 병목 개선** - 메일박스 변형, 스케줄링, 라우팅 최적화
3. **포괄적 성능 측정** - 자동화된 벤치마크 스크립트 및 데이터 시각화
4. **학술 수준 문서화** - 아키텍처 문서, 성능 분석 보고서, README 업그레이드
5. **포트폴리오 마무리** - GitHub 최적화, 데모 자료, 블로그 포스트

### 3.2 시스템 개선 사항

| 구분 | 개선 사항 | 예상 효과 | 우선순위 |
|------|----------|----------|----------|
| **메일박스** | MPSC 잠금 해제 메일박스 | 처리량 2-3배 향상 | 치명적 |
| **스케줄링** | 작업 도둑질 스케줄러 | 워커 간 로드 균형 | 높음 |
| **라우팅** | 이중 뮤텍스 제거, 배치 팝 | 오버헤드 50% 감소 | 높음 |
| **배치** | 적응적 maxBatch 조절 | 다양한 워크로드 최적화 | 중간 |
| **메트릭** | 잠금 해제 카운터 | 측정 오버헤드 감소 | 중간 |
| **동적 워커** | 동적 워커 조절 | 리소스 효율성 향상 | 중간 |

### 3.3 부차적 목표

1. 벤치마크 결과에 JSON 출력 형식 추가
2. 재현 가능한 측정을 위한 자동화된 벤치마크 스크립트 작성
3. 성능 데이터 시각화 (차트/그래프) 추가
4. 근거가 있는 아키텍처 결정 문서화
5. 관련 연구와의 비교 (Erlang/OTP, Akka, Tokio)

### 3.4 성공 기준

| 기준 | 측정 지표 |
|-----------|--------|
| 벤치마크 | 7개 이상의 벤치마크와 매개변수 설정 |
| 메일박스 변형 | 3개 구현 (뮤텍스, MPSC 잠금 해제, 기존 기준선) |
| 시스템 개선 | MPSC가 뮤텍스 대비 >2배 처리량, 작업 도둑질로 로드 균형 |
| 문서 | architecture.md + performance.md + 업데이트된 README.md |
| 재현성 | 일관된 결과를 내는 자동화된 벤치마크 스크립트 |
| 코드 품질 | 모든 테스트 통과, sanitizer 오류 없음 |

---

## 4. 단계 요약

### 일정 개요

```
월 1:  [1단계: 벤치마크 완성] ████████████████████
월 2:  [2단계: 시스템 개선]   ████████████████████
         [3단계: 측정]         ████████████████
월 3:  [4단계: 문서화]         ████████████████████████████
월 4:  [5단계: 포트폴리오 마무리] ████████████████
```

### 단계별 의존성

```
1단계 (벤치마크 완성)
  └──> 2단계 (시스템 개선) ──> 3단계 (측정)
                                  │
                                  v
                           4단계 (문서화)
                                  │
                                  v
                           5단계 (포트폴리오 마무리)
```

### 상세 단계 문서

- [1단계: 벤치마크 완성](phase1_benchmark.md)
- [2단계: 시스템 개선](phase2_improvements.md)
- [3단계: 측정](phase3_measurement.md)
- [4단계: 문서화](phase4_documentation.md)
- [5단계: 포트폴리오 마무리](phase5_portfolio.md)

---

## 5. 기술 아키텍처

### 5.1 현재 아키텍처

```
                    ┌─────────────────────┐
                    │    ActorSystem      │
                    │   (오케스트레이터)    │
                    └──────────┬──────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
              v                v                v
        ┌──────────┐    ┌──────────┐    ┌──────────┐
        │Scheduler │    │Dispatcher│    │ Registry │
        │(timerfd) │    │ (epoll)  │    │          │
        └──────────┘    └────┬─────┘    └──────────┘
                             │
                    ┌────────┴────────┐
                    │                 │
                    v                 v
              ┌──────────┐     ┌──────────┐
              │ Worker 0 │     │ Worker N │
              │(세마포어) │     │(세마포어) │
              └────┬─────┘     └────┬─────┘
                   │                │
                   v                v
              ┌──────────┐     ┌──────────┐
              │ActorCtx 0│     │ActorCtx N│
              │ ┌──────┐ │     │ ┌──────┐ │
              │ │메일박스│ │     │ │메일박스│ │
              │ │(뮤텍스)│ │     │ │(뮤텍스)│ │
              │ └──────┘ │     │ └──────┘ │
              │  액터    │     │  액터    │
              └──────────┘     └──────────┘
```

### 5.2 목표 아키텍처

```
                    ┌─────────────────────┐
                    │    ActorSystem      │
                    │   (오케스트레이터)    │
                    └──────────┬──────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
              v                v                v
        ┌──────────┐    ┌──────────┐    ┌──────────┐
        │Scheduler │    │Dispatcher│    │ Registry │
        │(timerfd) │    │ (epoll)  │    │          │
        └──────────┘    └────┬─────┘    └──────────┘
                             │
                    ┌────────┴────────┐
                    │                 │
                    v                 v
              ┌──────────┐     ┌──────────┐
              │ Worker 0 │     │ Worker N │
              │(세마포어) │     │(세마포어) │
              └────┬─────┘     └────┬─────┘
                   │                │
                   v                v
              ┌──────────┐     ┌──────────┐
              │ActorCtx 0│     │ActorCtx N│
              │ ┌──────┐ │     │ ┌──────┐ │
              │ │메일박스│ │     │ │메일박스│ │  <-- 플러그인 가능:
              │ │(MPSC) │ │     │ │(뮤텍스)│ │      뮤텍스 / MPSC / SPSC
              │ └──────┘ │     │ └──────┘ │
              │  액터    │     │  액터    │
              └──────────┘     └──────────┘
```

### 5.3 메일박스 변형 비교

| 측면 | MutexMailbox | MPSCMailbox (잠금 해제) | SPSCMailbox (잠금 해제) |
|--------|--------------|------------------------|------------------------|
| **동시성** | 뮤텍스를 통한 MPSC | 진정한 MPSC | 진정한 SPSC |
| **push() 오버헤드** | 뮤텍스 잠금/해제 | 원자적 fetch_add + CAS | 원자적 store (relaxed) |
| **pop() 오버헤드** | 뮤텍스 잠금/해제 | 원자적 load + store | 원자적 store (relaxed) |
| **캐시 라인 영향** | 단일 캐시 라인 공유 | 패딩된 head/tail | 패딩된 head/tail |
| **메모리 순서** | 해당 없음 (뮤텍스가 제공) | acquire/release 쌍 | acquire/release 쌍 |
| **적용 시나리오** | 낮은 경쟁 | 높은 다중 프로듀서 | 단일 프로듀서 보장 |
| **복잡도** | 낮음 | 중간 | 낮음 |
| **정확성 위험** | 낮음 | 중간 (메모리 순서) | 낮음 |

### 5.4 MPSC 메일박스 설계 (목표 구현)

Dmitry Vyukov의 바운드 MPMC 큐를 MPSC용으로 단순화한 것:

```
버퍼 레이아웃:
┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│ S0  │ S1  │ S2  │ S3  │ S4  │ S5  │ S6  │ S7  │  (용량 = 8)
└─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
  │                                         │
  head_ (원자적)                     tail_ (원자적)
  (프로듀서만)                       (컨슈머만)

각 슬롯:
┌─────────────────────────────────────┐
│ std::atomic<size_t> sequence        │  <-- 시퀀스 카운터
│ alignas(64) T data                  │  <-- 메시지 페이로드
└─────────────────────────────────────┘

푸시 (프로듀서):
1. pos = head_.fetch_add(1, relaxed)     // 슬롯 인덱스 확보
2. slot.sequence == pos가 될 때까지 대기  // 시퀀스 대기
3. 슬롯에 데이터 쓰기
4. slot.sequence.store(pos + 1, release) // 게시

팝 (컨슈머):
1. tail_을 읽어 현재 위치 확인
2. slot.sequence 읽기                    // 준비 확인
3. sequence == tail_ + 1이면:           // 데이터 사용 가능
   - 데이터 이동
   - tail_.store(tail_ + 1, release)     // tail 전진
4. 아니면: false 반환 (비어있음)
```

### 5.5 메모리 순서 다이어그램

```
프로듀서 스레드                          컨슈머 스레드
─────────────────                        ─────────────────
head_.fetch_add(relaxed)                 tail_.load(relaxed)
    │                                        │
    v                                        v
slot.data = std::move(msg)               slot.sequence.load(acquire)
    │                                        │
    v                                        v
slot.sequence.store(pos+1, release) ─────> 데이터가 보임
    │                                        │
    v                                        v
(컨슈머의 acquire load로 확인됨)            out = std::move(slot.data)
                                               │
                                               v
                                           tail_.store(new_tail, release)
```

---

## 6. 핵심 설계 결정

### 6.1 왜 MPSC (SPSC가 아닌)?

**관찰**: `ActorContext::enqueue()`는 여러 스레드에서 호출됩니다:
- 다른 액터들이 메시지 전송
- IPC 핸들러가 데이터 전달
- D-Bus 핸들러가 호출 라우팅
- 타이머 콜백
- 외부 CLI 명령

**결론**: 메일박스는 반드시 여러 동시 프로듀서를 지원해야 합니다. SPSC는 모든 전송을 단일 스레드를 통해 직렬화하도록 전체 메시지 라우팅을 재구성해야 하며, 이는 아키텍처적으로 침해적입니다.

**트레이드오프**: MPSC 잠금 해제는 SPSC보다 복잡하지만 기존 API와 아키텍처를 보존합니다.

### 6.2 왜 MPMC가 아닌?

**관찰**: `ActorContext::run()`은 항상 단일 워커 스레드에 의해 호출됩니다 (`scheduled_` 원자적 플래그로 보장).

**결론**: 컨슈머 측은 본질적으로 단일 스레드입니다. MPMC는 tail에 대한 CAS로 불필요한 복잡성을 추가합니다.

### 6.3 왜 MutexMailbox를 유지하는가?

**이유**:
1. **비교 기준선**: 학술적 성능 분석에 필수
2. **대체 수단**: 잠금 해제 구현에 가장자리 사례가 있을 수 있음; 뮤텍스 버전은 정확성이 증명됨
3. **낮은 경쟁 시나리오**: 메시지가 적은 액터의 경우 뮤텍스 오버헤드가 무시할 수 있음
4. **디버깅**: 뮤텍스 버전이 계측하고 디버깅하기 더 쉬움

### 6.4 메일박스 선택 전략

**런타임 선택** (포트폴리오 권장):
- ActorSystem 생성자가 `MailboxType` 열거형을 받음
- 각 ActorContext가 선택된 메일박스 타입 생성
- 벤치마크가 코드 변경 없이 모든 변형 비교 가능

**컴파일 시 선택** (대안):
- ActorSystem의 템플릿 매개변수
- 런타임 오버헤드 제로
- 벤치마크를 위한 유연성 낮음

**결정**: 벤치마크 유연성을 위한 런타임 선택, 프로덕션을 위한 컴파일 시 선택 옵션.

### 6.5 인터페이스 호환성

모든 메일박스 변형은 동일한 인터페이스를 구현해야 합니다:

```cpp
template <typename T>
class IMailbox {
public:
    virtual ~IMailbox() = default;
    virtual bool push(T&& msg) = 0;
    virtual bool pop(T& out) = 0;
    virtual bool empty() const = 0;
    virtual size_t count() const = 0;
    virtual size_t capacity() const = 0;
};
```

**참고**: 가상 디스패치 오버헤드는 벤치마크를 위해 허용됩니다. 프로덕션의 경우 CRTP 또는 템플릿 기반 정적 다형성을 고려하십시오.

---

## 7. 산출물

### 7.1 코드 산출물

| 파일 | 설명 |
|------|-------------|
| `src/core/actor_system/runtime/i_mailbox.hpp` | IMailbox 인터페이스 |
| `src/core/actor_system/runtime/mutex_mailbox.hpp` | mailbox.hpp에서 이름 변경 |
| `src/core/actor_system/runtime/mpsc_mailbox.hpp` | 잠금 해제 MPSC 구현 |
| `src/core/actor_system/runtime/spsc_mailbox.hpp` | 잠금 해제 SPSC (선택 사항) |
| `src/core/perf/benchmark/bench_latency.cpp` | 지연 시간 벤치마크 |
| `src/core/perf/benchmark/bench_contention.cpp` | 경쟁 벤치마크 |
| `src/core/perf/benchmark/bench_scaling.cpp` | 스케일링 벤치마크 |
| `src/core/perf/benchmark/bench_backpressure.cpp` | 백프레셔 벤치마크 |
| `src/core/perf/benchmark/bench_scheduler.cpp` | 스케줄러 벤치마크 |
| `src/core/perf/benchmark/bench_mailbox_comparison.cpp` | 메일박스 변형 비교 |
| `test/unit/core/test_mpsc_mailbox.cpp` | MPSC 메일박스 단위 테스트 |
| `test/benchmark/core/mpsc_mailbox_bench.cpp` | MPSC용 Google Benchmark |
| `scripts/benchmark_all.sh` | 자동화된 벤치마크 러너 |
| `scripts/visualize.py` | Matplotlib 시각화 |

### 7.2 문서 산출물

| 파일 | 설명 |
|------|-------------|
| `README.md` | 업그레이드된 영어 README |
| `docs/architecture.md` | 시스템 아키텍처 심층 분석 |
| `docs/performance.md` | 학술적 성능 분석 보고서 |
| `docs/mailbox_design.md` | 메일박스 변형 설계 상세 |
| `docs/benchmark_methodology.md` | 벤치마크 방법론 및 재현성 |

### 7.3 데이터 산출물

| 파일 | 설명 |
|------|-------------|
| `benchmark_results/throughput_*.csv` | 처리량 벤치마크 결과 |
| `benchmark_results/latency_*.csv` | 지연 시간 벤치마크 결과 |
| `benchmark_results/comparison_*.json` | 메일박스 비교 결과 |
| `benchmark_results/charts/*.png` | 시각화 차트 |

---

## 8. 위험 평가

### 8.1 기술적 위험

| 위험 | 확률 | 영향 | 완화 방안 |
|------|-------------|--------|------------|
| MPSC 메일박스 메모리 순서 버그 | 중간 | 높음 | 광범위한 단위 테스트, 스트레스 테스트, sanitizer 도구 |
| MutexMailbox 성능 회귀 | 낮음 | 중간 | 변경 전 기준선 벤치마크 |
| ActorContext 리팩토링이 기존 테스트 파괴 | 중간 | 높음 | 각 변경 후 전체 테스트 스위트 실행 |
| 벤치마크 방법론 결함 | 낮음 | 높음 | Google Benchmark 모범 사례 따르기, 여러 번 실행 |
| 컴파일러 최적화가 결과에 영향 | 중간 | 중간 | 컴파일러 플래그 문서화, 일관된 설정 사용 |

### 8.2 일정 위험

| 위험 | 확률 | 영향 | 완화 방안 |
|------|-------------|--------|------------|
| 1단계가 예상보다 오래 걸림 | 중간 | 높음 | 더 간단한 MPSC 설계로 시작, SPSC 연기 |
| 문서화 단계 범위 확장 | 높음 | 중간 | 고정 범위: architecture.md + performance.md만 |
| 측정 단계에 하드웨어 접근 필요 | 낮음 | 중간 | 현재 개발 머신 사용, 사양 문서화 |

### 8.3 포트폴리오 위험

| 위험 | 확률 | 영향 | 완화 방안 |
|------|-------------|--------|------------|
| 결과가 충분히 인상적이지 않음 | 낮음 | 높음 | 여러 비교 차원 포함 |
| 코드 품질 우려 | 낮음 | 중간 | 린터, 타입체커, sanitizer 실행 |
| 관련 연구 비교 누락 | 중간 | 중간 | Erlang/Akka/Tokio 메일박스 설계 조사 |

---

## 9. 참고 자료

### 9.1 학술 참고 문헌

1. **Hewitt, C., Bishop, P., & Steiger, R.** (1973). A Universal Modular ACTOR Formalism for Artificial Intelligence. IJCAI.
2. **Armstrong, J.** (2013). Erlang/OTP: A Concurrent Language for Reliable Software.
3. **Akka Documentation**. (2024). Typed Actors, Mailboxes, Dispatchers.
4. **Tokio Documentation**. (2024). MPMC bounded queue, Work-stealing scheduler.
5. **Vyukov, D.** (2014). Bounded MPMC Queue. https://github.com/dvaneeden/dbq
6. **Drepper, U.** (2011). Futexes Are Tricky. Ulrich Drepper's blog.
7. **Maged, M.** (2010). Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms.

### 9.2 구현 참고 문헌

1. **LMAX Disruptor**. (2011). 고성능 스레드 간 메시징 라이브러리.
2. **Folly MPSCQueue**. Facebook의 잠금 해제 MPSC 큐 구현.
3. **moodycamel::ConcurrentQueue**. Cameron Desrochers의 잠금 해제 큐.
4. **xenakios/kfifo**. Linux 커널의 잠금 해제 링 버퍼.

### 9.3 도구

1. **Google Benchmark** v1.9.2 (이미 프로젝트에 포함)
2. **GoogleTest** v1.17.0 (이미 프로젝트에 포함)
3. **ThreadSanitizer (TSan)** 데이터 레이스 감지용
4. **AddressSanitizer (ASan)** 메모리 오류 감지용
5. **perf** CPU 프로파일링 및 캐시 미스 분석용
6. **matplotlib** 시각화용
7. **gnuplot** 차트 생성 대안

---

## 부록 A: 현재 파일 구조

```
V2-Engine/
├── src/
│   ├── app/                    # 실행 파일 (v2_main, v2_cli, v2_tui)
│   ├── core/
│   │   ├── actor_system/       # 액터, ActorContext, ActorSystem, 메시지
│   │   │   ├── runtime/        # 디스패처, 워커, 메일박스, 스케줄러
│   │   │   └── actor/          # 액터 기본, ActorContext, ActorRegistry
│   │   ├── common/             # 유틸리티, Time, Log, Epoll, RingBuffer
│   │   └── perf/
│   │       ├── metrics/        # 메트릭 시스템
│   │       └── benchmark/      # 벤치마크 프레임워크 + 구현
│   ├── service/                # 서비스 액터 (IPC, D-Bus, 모니터 등)
│   └── infra/                  # HAL, 전송 (UDS, TCP 플레이스홀더)
├── test/
│   ├── unit/                   # 10개 단위 테스트 파일
│   ├── integration/            # 2개 통합 테스트
│   └── benchmark/              # 4개 Google Benchmark 스위트
├── docs/                       # 문서 (이 계획)
├── config/                     # JSON 설정 파일
├── README.md                   # 프로젝트 README
└── CMakeLists.txt              # 빌드 시스템
```

## 부록 B: Git 커밋 히스토리 요약

최근 개발 단계:
1. **코어 기반**: 액터 시스템, 디스패처, 워커, 메일박스, 서비스
2. **PMU 서브시스템**: 전원 관리 유닛 지원
3. **테스트 인프라**: GoogleTest 단위/통합 테스트
4. **벤치마크**: 링 버퍼, 메일박스, 타이머용 Google Benchmark 스위트
5. **WiFi/NetworkManager**: D-Bus 기반 WiFi 관리
6. **메트릭 + 벤치마크 프레임워크**: 프로세스 내 벤치마크 프레임워크 (현재)
