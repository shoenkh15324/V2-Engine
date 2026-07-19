# V2-Engine 로드맵

> C++20 경량 액터 모델 프레임워크 — 고성능 lock-free 메시징

---

## 현재 상태

| 지표 | 값 |
|------|-----|
| 최대 쓰루풋 | 7.1M msgs/sec (workers=2) |
| 최저 P50 레이턴시 | 378 ns (workers=1) |
| 테스트 | 117개 통과 |
| 메일박스 | LockFreeMailbox (기본) + MutexMailbox |

---

## 완료된 작업

### 벤치마크 구현 및 문서화 ✅

| 항목 | 상세 |
|------|------|
| 6개 벤치마크 구현 | throughput, latency, contention, scaling, backpressure, scheduler |
| CLI 통합 | `v2_cli benchmark <name>` |
| 벤치마크 문서화 | `docs/benchmark/` — 한국어, 표준 템플릿 |
| 종합 분석 | `docs/benchmark/README.md` |
| sleep-wait 제거 | spin-wait로 변경하여 측정 정확도 향상 |

### 메일박스 시스템 ✅

| 항목 | 상세 |
|------|------|
| IMailbox 인터페이스 | `push()`, `pop()`, `popBatch()`, `empty()`, `count()`, `capacity()` |
| MutexMailbox | `std::mutex` 기반, MPMC |
| LockFreeMailbox | CAS 기반 MPSC, **기본값** |
| 테스트 | 117개 통과 |
| 아키텍처 비교 문서 | `docs/architecture/mailbox_comparison.md` |

### 버그 수정 ✅

| 항목 | 상세 |
|------|------|
| CLI 인자 파싱 | `key=value` → `--key value` 변환 |
| scheduler | `res.throughput.iterations` → `res.scheduler.iterations` |

---

## Phase 1: Actor-Worker Affinity 디스패처

> **문제**: 멀티 워커에서 디스패처 뮤텍스/세마포어가 병목 → workers=4에서 668x 성능 저하

**목표**: 전역 뮤텍스/세마포어 제거 + 소비자 경쟁 제거

| 작업 | 상세 |
|------|------|
| Per-Worker MPSC 큐 | 전역 `readyQueue_` + `mutex_` → 워커별 `LockFreeMailbox<ActorContext*>` |
| Per-Worker 세마포어 | 전역 `counting_semaphore` → 워커별 세마포어 (thundering herd 제거) |
| 액터-워커 악피니티 | `hash(actorId) % workerCount`으로 고정 배정 → 한 액터를 한 워커만 처리 |
| 라운드로빈 오버플로우 | 타겟 큐 full 시 다음 큐에 push 시도 |
| `inQueue_` 제거 | `ActorContext::scheduled_` 원자적 교환으로 dedup 대체 |
| 테스트/벤치마크 업데이트 | `test_dispatcher.cpp` 수정 + 벤치마크 재측정 |

**예상 효과**: workers=4에서 10~50x throughput 향상 (동기화 비용 300-600ns → 65-120ns)

**변경 파일**: `dispatcher.hpp/cpp`, `actor_context.cpp`, `worker.cpp`, `test_dispatcher.cpp`

---

## Phase 2: 메모리/전송 최적화

> **목표**: 캐시 라인 활용 + 불필요한 할당/조회 제거

| 작업 | 상세 |
|------|------|
| 캐시 라인 패딩 | `alignas(64)` 핫 데이터 + power-of-2 링 버퍼 |
| ActorRef | `actor_ref.hpp` — 매 send마다 이름 조회 제거 |
| TimerNode | `shared_ptr` 제거, 힙 할당 0회 |

---

## Phase 3: 아키텍처 고도화

> **목표**: 타입 안전 + 장애 처리 + 로드 밸런싱

| 작업 | 상세 |
|------|------|
| 타입별 메시지 디스패치 | `typed_channel.hpp` — variant 대신 타입 안전 액터 |
| Supervision 트리 | `supervisor.hpp` — 액터 실패 처리/재시작 |
| Work Stealing | `work_stealing_queue.hpp` — Phase 1 악피니티 핫스팟 보완 |

---

## Phase 4: 벤치마크 + 학술 보고서

> **목표**: 학술 수준 성능 분석 + 포트폴리오

| 작업 | 상세 |
|------|------|
| 반복 측정 | 10회+ 반복, 평균 ± 표준편차, 95% 신뢰구간 |
| 영어 보고서 | Abstract ~ Conclusion, 학술 수준 |
| GitHub 정리 | README, 토픽, 데모 자료 (GIF/스크린샷) |

---

## 일정

```
Phase 1: Actor-Worker Affinity  [3주]  █████████████
Phase 2: 메모리/전송 최적화      [3주]              █████████████
Phase 3: 아키텍처 고도화          [4주]                      ████████████████
Phase 4: 벤치마크 + 보고서        [4주]                                  ████████████████

총: 14주 (~3.5개월)
```
