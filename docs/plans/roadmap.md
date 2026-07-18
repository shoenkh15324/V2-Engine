# V2-Engine 개선 로드맵

> **목표**: 이직 준비 + 대학원 컨택 동시 진행 (6개월)
> **언어**: 한국어 기획 / 영어 학술 문서

---

## 완료된 작업

### 벤치마크 구현 및 문서화 ✅

| 항목 | 상태 | 상세 |
|------|------|------|
| 6개 벤치마크 구현 | ✅ 완료 | throughput, latency, contention, scaling, backpressure, scheduler |
| CLI 통합 | ✅ 완료 | `v2_cli benchmark <name>` 명령어 동작 |
| 벤치마크 문서화 | ✅ 완료 | `docs/benchmark/` — 한국어, 표준 템플릿 통일 |
| 종합 분석 | ✅ 완료 | `docs/benchmark/README.md` — 시스템 전체 평가 |
| sleep-wait 제거 | ✅ 완료 | spin-wait로 변경하여 측정 정확도 향상 |
| CLI 인자 파싱 버그 수정 | ✅ 완료 | `key=value` → `--key value` 변환 |
| scheduler 버그 수정 | ✅ 완료 | `res.throughput.iterations` → `res.scheduler.iterations` |

### 메일박스 시스템 ✅

| 항목 | 상태 | 상세 |
|------|------|------|
| IMailbox 인터페이스 | ✅ 완료 | `push()`, `pop()`, `popBatch()`, `empty()`, `count()`, `capacity()` |
| MutexMailbox 구현 | ✅ 완료 | `std::mutex` 기반, MPMC 가능 |
| LockFreeMailbox 구현 | ✅ 완료 | CAS 기반 MPSC, **기본값** |
| MailboxType 열거형 | ✅ 완료 | `Mutex`, `LockFree` 런타임 선택 |
| ActorContext 통합 | ✅ 완료 | `unique_ptr<IMailbox<Message>>` 기반 |
| 테스트 업데이트 | ✅ 완료 | 117개 테스트 전부 통과 |
| 아키텍처 비교 문서 | ✅ 완료 | `docs/architecture/mailbox_comparison.md` |

### 현재 시스템 핵심 결과

| 지표 | 값 | 조건 |
|------|-----|------|
| **최대 쓰루풋** | **7.1M msgs/sec** | workers=2, LockFreeMailbox |
| **최저 P50** | **378 ns** | workers=1 |
| **최저 P99** | **641 ns** | workers=1 |
| **최대 동시 push** | **7.97M msgs/sec** | 프로듀서 2개 |
| **타이머 정확도** | **100%** | 모든 간격/워커 |
| **타이머 드리프트** | **0.02%** | interval=50ms |

---

## 향후 로드맵

### Phase 1: 디스패처 최적화

> **목표**: 뮤텍스 병목 제거

| 작업 | 상태 | 산출물 |
|------|------|--------|
| LockFreeMailbox 구현 | ✅ 완료 | `mailbox_lockfree.hpp` + 테스트 |
| 기본값 적용 | ✅ 완료 | `MailboxType::LockFree` |
| 벤치마크 비교 | ✅ 완료 | `docs/architecture/mailbox_comparison.md` |

**남은 작업:**

| 작업 | 산출물 |
|------|--------|
| Lock-free Dispatcher | `lock_free_dispatcher.hpp` — readyQueue 뮤텍스 제거 |
| `inQueue_` 제거 | scheduled_ atomic 활용으로 중복 제거 |
| 배치 pop 최적화 | 기존 코드 리팩터링 + 벤치마크 비교 |

**IMailbox 인터페이스 (완성):**

```cpp
template <typename T>
class IMailbox {
public:
    virtual ~IMailbox() = default;
    virtual bool push(T&& msg) = 0;
    virtual bool pop(T& out) = 0;
    virtual size_t popBatch(T* out, size_t max) = 0;
    virtual bool empty() const = 0;
    virtual size_t count() const = 0;
    virtual size_t capacity() const = 0;
};
```

**검증**: 단위 테스트 통과 ✅, 벤치마크 완료 ✅

---

### Phase 2: 성능 튜닝 (3주)

> **목표**: 메모리 오버헤드 최소화

| 주차 | 작업 | 산출물 |
|------|------|--------|
| 1 | **캐시 라인 패딩** (`alignas(64)` 핫 데이터) + **power-of-2 링 버퍼** | 메모리 레이아웃 최적화 |
| 2 | **ActorRef 캐싱** | `actor_ref.hpp` — 매 sendMsg마다 이름 조회 제거 |
| 3 | **TimerNode 인트루시브 컨테이너** | `shared_ptr` 제거, 힙 할당 0회 |

**ActorRef 설계:**

```cpp
class ActorRef {
    ActorContext* ctx_;  // 생성 시 해석된 포인터
public:
    void sendMsg(Message msg);  // 이름 조회 없이 직접 enqueue
};
```

---

### Phase 3: 아키텍처 개선 (4주)

> **목표**: 아키텍처 고도화 + 학술적 깊이

| 주차 | 작업 | 산출물 |
|------|------|--------|
| 1-2 | **타입별 메시지 디스패치** | `typed_channel.hpp` — variant 대신 typed actor |
| 3 | **Supervision 트리** | `supervisor.hpp` — 액터 실패 처리/재시작 |
| 4 | **Work stealing** | `work_stealing_queue.hpp` — 워커 간 작업 이동 |

**타입별 메시지 설계:**

```cpp
// 기존: 단일 variant
using Message = std::variant<Tick, Heartbeat, DataReady, ...>;  // 27개 타입

// 개선: 타입별 채널
template<typename... MsgTypes>
class TypedActor : public Actor {
    void handle(const Message& msg) override;  // 타입 안전
};
```

---

### Phase 4: 벤치마크 고도화 (4주)

> **목표**: 학술 수준 측정 + 통계 분석

| 주차 | 작업 | 산출물 |
|------|------|--------|
| 1 | **벤치마크 재설계** (개선 사항 반복) | 새 벤치마크 코드 |
| 2 | **반복 측정** (10회+) + **통계 분석** | 평균, 표준편차, 95% 신뢰구간 |
| 3 | **영어 보고서** (Abstract~Conclusion) | 학술 수준 문서 |
| 4 | **포트폴리오 정리** | GitHub README + 1페이지 요약 |

**통계 분석 요구 사항:**

- 각 설정 최소 10회 반복
- 평균 ± 표준편차 보고
- 변동 계수 < 5% 확인
- 이상치 감지 및 필터링
- 95% 신뢰구간 계산

---

### Phase 5: 포트폴리오 마무리 (2주)

> **목표**: 대학원 컨택 + 이직 지원 자료

| 주차 | 작업 | 산출물 |
|------|------|--------|
| 1 | **GitHub 최적화** (설명, 토픽, 뱃지) | 업데이트된 저장소 |
| 2 | **데모 자료** (GIF, 스크린샷) | 시각적 임팩트 |

**GitHub 저장소 설정:**

| 설정 | 값 |
|------|-----|
| 설명 | "C++20 lightweight actor model framework with lock-free MPSC mailbox" |
| 토픽 | `cpp20`, `actor-model`, `lock-free`, `concurrency`, `embedded-linux` |

---

## 병목 분석 요약

### 현재 뮤텍스 병목 순위

| 순위 | 위치 | 심각도 | 설명 |
|------|------|--------|------|
| ~~1~~ | ~~`Mailbox::mutex_`~~ | ~~**CRITICAL**~~ | ~~push/pop 동일 뮤텍스 → 모든 메시지 경쟁~~ **✅ 해소** |
| 1 | `Dispatcher::mutex_` | **CRITICAL** | 모든 dispatch/acquire가 단일 뮤텍스 통과 |
| 2 | `ActorRegistry::mutex_` | **MEDIUM** | 매 sendMsg마다 string 기반 이름 조회 |
| 3 | `Timer::shared_ptr` | **MEDIUM** | 타이머마다 힙 할당 2회 |

### 메시지 전송 경로 (현재)

```
Actor::sendMsg()
  → ActorRegistry::findByName()    [뮤텍스 #2]
  → LockFreeMailbox::push()        [락프리 ✅]
  → Dispatcher::dispatch()         [뮤텍스 #1 ← 현재 최대 병목]
```

**단일 sendMsg() 호출이 뮤텍스 2개를 연쇄적으로 획득** (기존 3개에서 1개 감소)

---

## 검증 체크리스트

### Phase 1 검증

- [x] Lock-free 메일박스가 모든 IMailbox 인터페이스 구현
- [x] FIFO 순서 유지
- [x] TSAN 오류 없음
- [x] 벤치마크에서 뮤텍스 대비 개선 확인 (workers=1~2에서 6~8x)

### Phase 2 검증

- [ ] 캐시 패딩이 false sharing 방지
- [ ] ActorRef가 이름 조회 없이 동작
- [ ] TimerNode가 힙 할당 없이 동작

### Phase 3 검증

- [ ] 타입별 메시지가 기존 variant와 호환
- [ ] Supervision이 액터 실패 시 재시작
- [ ] Work stealing이 로드 균형 개선

### Phase 4 검증

- [ ] 10회 반복 측정 완료
- [ ] 변동 계수 < 5%
- [ ] 영어 보고서 완성

### Phase 5 검증

- [ ] GitHub 저장소 최적화 완료
- [ ] 데모 자료 준비 완료

---

## 참고 자료

### 학술 참고 문헌

1. Hewitt, C., Bishop, P., & Steiger, R. (1973). A Universal Modular ACTOR Formalism for Artificial Intelligence. IJCAI.
2. Armstrong, J. (2013). Erlang/OTP: A Concurrent Language for Reliable Software.
3. Akka Documentation. (2024). Typed Actors, Mailboxes, Dispatchers.
4. Tokio Documentation. (2024). MPMC bounded queue, Work-stealing scheduler.
5. Vyukov, D. (2014). Bounded MPMC Queue. https://github.com/dvaneeden/dbq
6. Drepper, U. (2011). Futexes Are Tricky.
7. Maged, M. (2010). Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms.

### 구현 참고 문헌

1. LMAX Disruptor. 고성능 스레드 간 메시징 라이브러리.
2. Folly MPSCQueue. Facebook의 잠금 해제 MPSC 큐 구현.
3. moodycamel::ConcurrentQueue. Cameron Desrochers의 잠금 해제 큐.

---

## 일정 요약

```
Phase 1: Mailbox 최적화      [완료]  ✅
Phase 2: 성능 튜닝              [3주]  ███████████████
Phase 3: 아키텍처 개선          [4주]           ████████████████████
Phase 4: 벤치마크 고도화        [4주]                      ████████████████████
Phase 5: 포트폴리오 마무리      [2주]                                  ████████████

총: 13주 (~3개월)
```

**병렬 가능**: Phase 2-3은 코드 작업, Phase 4-5는 문서 작업으로 병렬 진행 가능.
