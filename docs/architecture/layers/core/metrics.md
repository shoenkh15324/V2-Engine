# 성능 메트릭 (`src/core/perf/metrics/`)

엔진 내부에서 무슨 일이 벌어지는지 **숫자로** 보여주는 메트릭 시스템.
카운터가 어떻게 설계됐고, 누가 언제 기록하고, 어떻게 읽는지 처음 읽는 사람도 따라올 수 있게 정리한 문서.

---

## 목차

- [개요 — 왜 메트릭인가](#개요--왜-메트릭인가)
- [세 그룹 카운터](#세-그룹-카운터)
  - [ActorMetrics — 액터별](#actormetrics--액터별)
  - [WorkerMetrics — 워커별](#workermetrics--워커별)
  - [DispatcherMetrics — 디스패처 전역](#dispatchermetrics--디스패처-전역)
- [캐시라인 정렬 — 측정이 성능을 해치지 않게](#캐시라인-정렬--측정이-성능을-해치지-않게)
- [기록 흐름 — 누가 언제 부르나](#기록-흐름--누가-언제-부르나)
- [Metrics 클래스 API](#metrics-클래스-api)
- [접근자 패턴 — V2_METRICS()](#접근자-패턴--v2_metrics)
- [저오버헤드 설계](#저오버헤드-설계)
- [벤치마크와의 연계](#벤치마크와의-연계)

---

## 개요 — 왜 메트릭인가

> 📌 이 철학이 가장 잘 드러난 실화가 [작업 분배 문서](../../concepts/work_dispatch.md)의 "스케일링 붕괴 진단기"입니다.
> "추측으로 고치지 말고, 카운터를 달아 원인을 숫자로 고정한 뒤 고친다."

고성능 동시성 코드의 버그는 겉으로 멀쩡해 보이지만 숫자로는 명확합니다. "워커를 늘렸는데 왜 느려지지?" 같은 질문에 답하려면 평소부터 유휴 시간, 배치 크기, 중복 거절 횟수 같은 지표가 기록되어 있어야 합니다.

V² Engine의 메트릭 시스템은 그래서 **운영 경로에 항상 붙어 있지만**(비활성화 가능), 활성화돼도 핫 패스를 거의 느리게 만들지 않도록 설계되었습니다.

---

## 세 그룹 카운터

**파일:** `src/core/perf/metrics/metrics.hpp`

카운터는 관찰 대상별로 세 그룹으로 나뉩니다.

### ActorMetrics — 액터별

| 카운터 | 의미 | 기록 위치 |
|--------|------|-----------|
| `enqueued` | 메일박스 push 성공 수 | `ActorRuntime::enqueue` |
| `dropped` | 메일박스 가득 → 드롭된 메시지 수 | `ActorRuntime::enqueue` |
| `processed` | `handle()`로 처리된 메시지 수 | `ActorRuntime` 배치 종료 |
| `batches` | `run()` 호출(배치) 횟수 | `ActorRuntime` 배치 종료 |
| `handleTimeNs` | 처리에 소요한 누적 나노초 | `ActorRuntime` 배치 종료 |
| `peakDepth` | 메일박스 최대 깊이 (best-effort 피크) | `recordEnqueue` |
| `deadLetters` | 미매칭/실패 메시지 수 | `Actor::handleUnknown` |

액터 ID가 배열 인덱스라서 조회 비용 없이 바로 해당 액터의 구조체에 접근합니다.

### WorkerMetrics — 워커별

| 카운터 | 의미 | 기록 위치 |
|--------|------|-----------|
| `batches` | acquire→run 사이클 횟수 | `Worker::runLoop` |
| `busyTimeNs` | 실제 `run()` 중이던 누적 시간 | `Worker::runLoop` |
| `idleTimeNs` | 작업 대기(세마포어·스핀) 누적 시간 | `Worker::runLoop` |
| `messages` | 처리한 총 메시지 수 | `Worker::runLoop` |

busy/idle 비율은 건강 진단의 핵심입니다. idle이 비정상적으로 크면
"wakeup 비용" 문제([work_dispatch.md](../../concepts/work_dispatch.md))를 의심합니다.

### DispatcherMetrics — 디스패처 전역

| 카운터 | 의미 | 기록 위치 |
|--------|------|-----------|
| `dispatchCount` | `dispatch()` 호출 수 | `WorkDispatcher::dispatch` |
| `deduplicated` | 이미 살아있는 토큰 때문에 거절된 발행(dedup) 수 | `WorkDispatcher::dispatch` |
| `acquireCount` | 워커의 토큰 획득 시도 수 | `WorkDispatcher::acquire` |
| `stealCount` / `stealFailCount` | 작업 스틸링 성공/실패 수 | `WorkDispatcher::trySteal` |
| `readyQueuePeak` | 준비 큐 최대 깊이 | `recordDispatch` |

`deduplicated`가 크다는 건 같은 액터로 트래픽 몰림이 심하다는 뜻 — 토큰 dedup 게이트([work_dispatch.md](../../concepts/work_dispatch.md))가 그만큼 일하고 있다는 자연 스케일의 신호입니다.

---

## 캐시라인 정렬 — 측정이 성능을 해치지 않게

모든 카운터 구조체의 필드마다 이렇게 선언되어 있습니다:

```cpp
struct ActorMetrics{
    alignas(kCacheLine) std::atomic<uint64_t> enqueued{0};
    alignas(kCacheLine) std::atomic<uint64_t> processed{0};
    // ...
};
```

카운터 하나가 수정될 때마다 그 변수가 사는 64바이트(캐시 라인) 통째로 해당 코어의 L1 캐시에 독점됩니다. 카운터들을 빈틈없이 붙여두면 서로 다른 스레드가 *다른* 카운터를 건드려도 같은 캐시 라인을 두고 싸우는 **거짓 공유(false sharing)**가 생깁니다. 한 줄당 캐시 라인을 독점시켜 원천 차단하는 것입니다. (`kCacheLine`의 값 결정은 [동시성 문서](../../concepts/concurrency.md) 참고.)

---

## 기록 흐름 — 누가 언제 부르나

```
메시지 전송        ActorRuntime::enqueue ────► recordEnqueue(id, 성공?, mailbox 깊이)
                                              ├─ 성공: enqueued++, peakDepth 갱신
                                              └─ 실패: dropped++

배치 처리 완료     ActorRuntime::run ────────► recordHandle(id, 처리 수, 소요 ns)
                                              processed += n, batches++,
                                              handleTimeNs += gap

토큰 발행          WorkDispatcher::dispatch ► recordDispatch(dedup 여부, 큐 깊이)

워커 사이클        Worker::runLoop ─────────► recordAcquire() ×획득 경로
                   스틸 시도                  recordSteal(성공?)
                   사이클 종료                recordBatch(id, msg수, busyNs, idleNs)

미매칭 메시지      Actor::handleUnknown ────► recordDeadLetter(id)
```

기록은 전부 **fire-and-forget**입니다. fetch_add 한 방이면 끝나고, 아무도 이 값을 기다리지 않습니다. 조회는 별도의 `snapshot()` 호출로만 이뤄집니다.

---

## Metrics 클래스 API

```cpp
class Metrics{
public:
    void init(size_t numWorkers);            // 워커 수만큼 WorkerMetrics 생성
    void registerActor(uint64_t actorId);    // 필요하면 ActorMetrics 배열 확장
    void setEnabled(bool);                   // ★ 마스터 스위치

    // 기록 (위 흐름도 참조)
    void recordEnqueue(uint64_t actorId, bool success, size_t depth);
    void recordHandle(uint64_t actorId, size_t count, uint64_t durationNs);
    void recordBatch(int workerId, size_t msgCount, uint64_t busyNs, uint64_t idleNs);
    void recordDispatch(bool deduped, size_t queueDepth);
    void recordAcquire();
    void recordSteal(bool success);
    void recordDeadLetter(uint64_t actorId);

    // 조회
    Snapshot snapshot();   // 모든 액터 + 워커 + 디스패처의 일관된 복사본
    void reset();          // 전체 0으로
};
```

`Snapshot`은 원자 변수를 relaxed로 읽어 평범한 구조체에 담아 돌려줍니다. 로그 출력, TUI, 벤치마크 드라이버가 모두 이 스냅샷만 소비합니다 — 원자 변수가 밖으로 새어나가지 않습니다.

---

## 접근자 패턴 — V2_METRICS()

로거와 동일한 글로벌 교체 패턴입니다:

```cpp
Metrics& activeMetrics();              // 활성 인스턴스 (없으면 fallback)
void setActiveMetrics(Metrics* m);     // 조립 루트가 기동 초기에 1회 설정
#define V2_METRICS() (&activeMetrics())
```

호출부는 어디서든 `V2_METRICS()->recordEnqueue(...)` 한 줄입니다. 설정은 `main_app.cpp:81-82`에서:

```cpp
setActiveMetrics(&metrics_);
metrics_.setEnabled(cfg_.enableMetrics);   // config/v2_main.json의 enable_metrics
```

아무도 설정하지 않으면 fallback 인스턴스가 받아주므로, 코어 단독 유닛 테스트 (v2_core_smoke 등)에서도 메트릭 코드가 안전하게 동작합니다.

---

## Low 오버헤드 설계

메트릭이 "공짜"는 아니지만, 다음 장치들로 비용을 최소화합니다:

| 장치 | 효과 |
|------|------|
| `enabled_` 게이트 | 비활성화 시 record*가 **즉시 return** — 분기 1회 비용 |
| `memory_order_relaxed` | fetch_add/store에 펜스 없음 — 원자성만 보장 |
| 배열 인덱싱(actorId → 구조체) | 해시/락 조회 없음 |
| 캐시라인 정렬 | 측정 자체가 거짓 공유를 만들지 않음 |
| fire-and-forget | 기록이 누군가를 블록하지 않음 |
| snapshot 시점에만 집계 | 집계 비용을 관찰자에게 전가 |

정확히 말하면 relaxed 카운터는 "순간 정확한" 값이 아니라 **근사치**입니다.
성능 진단 용도에는 충분하고, 그래서 통계 조회 함수들도 relaxed로 읽습니다.

---

## 벤치마크와의 연계

벤치마크 CLI는 `V2_DIAG=1` 환경변수로 이 메트릭을 노출합니다
(측정법 상세: [work_dispatch.md §10](../../concepts/work_dispatch.md)):

```bash
V2_DIAG=1 ./build/Bench/v2_bench_cli throughput --workers 4 --actors 16 ...
```

출력되는 `[Diag]` 블록과 메트릭 출처:

| Diag 지표 | 출처 | 건강한 상태 |
|-----------|------|-------------|
| avgBatch | `DispatcherMetrics` + 토큰 사이클 | maxBatch=32에 가까울수록 좋음 |
| busyMs | `WorkerMetrics.busyTimeNs` | — |
| idleMs | `WorkerMetrics.idleTimeNs` | 작을수록 좋음 (크면 wakeup 비용 의심) |
| dedup | `DispatcherMetrics.deduplicated` | 트래픽 몰림 정도의 자연 스케일 |

판별 규칙도 그대로: **idleMs↑ = wakeup 문제 / busyMs↑ + 낮은 처리량 = 캐시·라우팅 문제.**

더 많은 결과표는 [벤치마크 문서](../../../benchmark/README.md)를 참고하세요.
