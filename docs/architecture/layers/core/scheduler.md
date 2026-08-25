# 스케줄러 — 액터 타이머 연결 계층

"5초 뒤에 이 메시지를 이 액터에게 보내줘"를 구현하는 `Scheduler`와
그 아래 타이머 장치들(`ITimer`/`TimerBase`)의 역할 분담을 처음 읽는
사람도 따라올 수 있게 정리한 문서.

> [액터 모델](../../concepts/actor_model.md)의 "타이머" 절에서 API
> (`startTimer`/`cancelTimer`)를 소개했습니다. 이 문서는 그 아래에서
> 실제로 일어나는 일을 다룹니다. OS 연동(timerfd vs std 스레드)은
> [인프라 문서](../../concepts/infrastructure.md) 참고.

---

## 목차

- [개요 — 두 층으로 나뉜 타이머](#개요--두-층으로-나뉜-타이머)
- [역할 분담 — Scheduler vs TimerBase](#역할-분담--scheduler-vs-timerbase)
- [등록 흐름 — startTimer()의 여정](#등록-흐름--starttimer의-여정)
- [발화 흐름 — 시간이 되면](#발화-흐름--시간이-되면)
  - [왜 clone()인가](#왜-clone인가)
  - [콜백의 안전 설계](#콜백의-안전-설계)
- [취소와 정리](#취소와-정리)
- [종료 순서](#종료-순서)
- [전체 구조도](#전체-구조도)

---

## 개요 — 두 층으로 나뉜 타이머

액터에게 "나중에 알려주기"는 사실 두 가지 관심사가 섞인 일입니다:

1. **시간을 재는 것** — 언제 울릴까? (최소힙, 만료 판단, OS 웨이크업)
2. **누구에게 무엇을 전달할까** — 만료됐을 때 어느 액터 메일박스에 어떤 메시지를 넣을까?

V² Engine은 이 둘을 별도 클래스로 분리합니다:

```
Actor::startTimer(msg, delayMs, repeating)     [편의 API]
        │
ActorRuntime::addTimer()                       [소유 추적 + 위임]
        │
Scheduler (src/core/actor_system/runtime/scheduler/)     ★ "누구에게 무엇을"
        │ ITimer 포트 경유
TimerBase / LinuxTimer / Timer                 ★ "언제 울릴까"
```

---

## 역할 분담 — Scheduler vs TimerBase

| | Scheduler | TimerBase |
|--|-----------|-----------|
| **파일** | `runtime/scheduler/scheduler.cpp` | `common/timer/timer_base.cpp` |
| **책임** | 타이머 ID → (대상 액터, 메시지) 매핑 | 지연 등록, 최소힙 관리, 만료 판단, 반복 재예약 |
| **아는 것** | IActorRuntime 포인터, Message | time_point, 콜백 함수 포인터 |
| **모르는 것** | 시계가 어떻게 도는지 | 메시지가 뭔지, 액터가 누군지 |

**파일:** `scheduler.hpp`

```cpp
struct TimerCtx{
    IActorRuntime* target;   // 받을 액터의 런타임
    Message msg;             // 전달할 메시지 원본
};

std::mutex mutex_;
std::unique_ptr<ITimer> timer_;                       // 하부 시계 (포트)
std::unordered_map<int, std::unique_ptr<TimerCtx>> timerCtxs_;  // id → ctx
```

Scheduler는 생성자에서 `ITimer`를 주입받고, 없으면 표준 스레드 기반
`Timer`를 폴백으로 만듭니다:

```cpp
Scheduler::Scheduler(std::unique_ptr<ITimer> timer){
    timer_ = timer ? std::move(timer) : std::make_unique<Timer>();
}
```

이 덕분에 코어만으로도(테스트 환경 등) 타이머가 동작하고,
프로덕션에서는 인프라의 `LinuxTimer`(timerfd)가 꽂혀 전용 스레드 없이
이벤트 루프와 함께 동작합니다([infrastructure.md](../../concepts/infrastructure.md)).

---

## 등록 흐름 — startTimer()의 여정

액터가 `startTimer(Tick{}, 1000, true)`를 부르면:

```
① Actor::startTimer()
      └─ runtime()->addTimer(this, msg, delay, repeating)

② ActorRuntime::addTimer()                    [actor_runtime.cpp:143]
      ├─ timerMutex_ 잠금                     ← 내 타이머 ID 목록 보호
      └─ scheduler_->addTimer(targetRuntime, msg, delay, repeating)
         성공하면 timerIds_에 id 기록          (소멸 때 자동 정리용)

③ Scheduler::addTimer()
      ├─ mutex_ 잠금
      ├─ cleanupTimerCtxs()                   ← 죽은 ctx 청소 (§취소)
      ├─ TimerCtx{target, msg} 생성
      ├─ id = timer_->add(delayMs, repeating, timerCallback, this)
      │                        ▲               ▲
      │              만료 시 불릴 함수   자기 자신을 컨텍스트로
      └─ timerCtxs_[id] = ctx
```

포인트: **각 층은 자기 데이터만 추가합니다.**
- ActorRuntime은 `timerIds_` (내가 건 타이머 목록 → 소멸자 정리용)
- Scheduler는 `timerCtxs_` (id가 누구에게 무엇을 보낼지)
- TimerBase는 최소힙 노드 (언제 울릴지만)

---

## 발화 흐름 — 시간이 되면

TimerBase가 만료를 감지하면(스레드 폴백이든 timerfd든 공통 로직
`handleTimerEvent()`), 등록 시 넘겨준 콜백을 부릅니다:

```cpp
void Scheduler::timerCallback(int id, void* ctx){
    auto* self = static_cast<Scheduler*>(ctx);
    std::lock_guard<std::mutex> lock(self->mutex_);
    auto it = self->timerCtxs_.find(id);
    if(it == self->timerCtxs_.end()) return;       // ① 취소됐으면 무시
    try{
        it->second->target->enqueue(it->second->msg.clone());   // ② 복제해서 배달
    }catch(const std::exception& e){ ... }         // ③ 예외 격리
}
```

배달은 특별한 통로가 아니라 그냥 평범한 `enqueue`입니다 — 즉 타이머 메시지도
[메시지 파이프라인](../../concepts/messaging.md)(메일박스 push → 디스패치 →
워커 → handle)을 정상적으로 타서, 대상 액터의 홈 워커에서 순서대로 처리됩니다.

### 왜 clone()인가

```cpp
target->enqueue(ctx->msg.clone());    // 원본이 아니라 '복제본'을 배달
```

원본(`ctx->msg`)은 다음 발화를 위해 Scheduler가 계속 보관해야 합니다.
반복 타이머라면 같은 메시지를 여러 번 보내야 하고요. 원본을 move로
넘겨버리면 두 번째 발화부터 빈 메시지가 전달되는 버그가 됩니다.
그래서 **매 발화마다 복제본**을 만듭니다 ([messaging.md §복제](../../concepts/messaging.md)).
복사 불가 타입이면 빈 메시지가 가므로, 타이머에 쓸 메시지 타입은 복사 가능해야 합니다.

### 콜백의 안전 설계

타이머 콜백은 종종 전용 스레드(LinuxTimer면 이벤트 루프 스레드)에서
불립니다. 여기서 예외가 새면 **시계 전체가 죽습니다**. 세 겹의 방어:

1. **취소된 id 무시** — find 실패는 조용히 return. cancel과 발화가 겼을 때 "삭제된 ctx 접근" 크래시가 나지 않습니다.
2. **예외 완전 격리** — enqueue가 던질 수 있는 예외를 catch해 로그만 남깁니다.
3. **mutex_ 단일 진입점** — 등록·취소·발화가 모두 같은 락을 통과하므로 ctx 맵의 상태가 항상 일관됩니다.

---

## 취소와 정리

**명시적 취소** (`cancelTimer`):

```cpp
void Scheduler::cancel(int id){
    std::lock_guard lock(mutex_);
    timer_->cancel(id);        // 힙에서 제거/무효화
    timerCtxs_.erase(id);      // ctx 제거
}
```

**묵시적 청소** (`cleanupTimerCtxs`) — addTimer마다 실행:

```cpp
for(auto it = timerCtxs_.begin(); it != timerCtxs_.end();){
    if(!timer_->isAlive(it->first))
        it = timerCtxs_.erase(it);     // 이미 발화 끝난 1회성 타이머
    else
        ++it;
}
```

1회성(non-repeating) 타이머는 발화 후 힙에서 사라지지만 ctx 맵에는 남습니다.
매 등록 시 살아있는 타이머만 골라 청소해 맵이 무한히 자라지 않게 합니다.

**액터 소멸 시 자동 정리** — `ActorRuntime::~ActorRuntime()`이
`timerIds_`의 모든 id를 `scheduler_->cancel()`합니다. 액터가 죽었는데
그 액터로 향하는 타이머가 살아남아 죽은 액터에게 메시지를 쏘는 일이
구조적으로 불가능합니다([actor_system.md](actor_system.md)의 견고성과 연결).

---

## 종료 순서

```cpp
void Scheduler::stop(){
    timer_->stop();            // ① 먼저 시계를 멈춘다 (새 발화 차단)
    std::lock_guard lock(mutex_);
    timerCtxs_.clear();        // ② 그다음 ctx를 지운다
}
```

순서가 중요합니다. ctx를 먼저 지우면 발화 중인 콜백이 이미 해제된 ctx에
접근할 수 있지만, **시계를 먼저 멈추면** 콜백이 더 이상 불리지 않으므로
안전하게 지울 수 있습니다. `ActorSystem::stop()`도 스케줄러를 가장 먼저
멈추는 이유입니다([actor_system.md §종료](actor_system.md)).

---

## 전체 구조도

```
                    ┌────────────────────────────────────────┐
                    │ Actor                                  │
                    │   startTimer(msg, delay, repeating)    │
                    └───────────────┬────────────────────────┘
                                    ▼
┌───────────────────────────────────────────────────────────────┐
│ ActorRuntime                                                  │
│   timerIds_ 집합 (timerMutex_)  ← 소유 추적, 소멸 때 일괄 취소 │
└───────────────┬───────────────────────────────────────────────┘
                ▼
┌───────────────────────────────────────────────────────────────┐
│ Scheduler                          "누구에게 무엇을"           │
│   timerCtxs_: id → {IActorRuntime*, Message}   (mutex_)       │
│   timerCallback: 조회 → clone() → target->enqueue()           │
└───────────────┬───────────────────────────────────────────────┘
                │ ITimer 포트
                ▼
┌───────────────────────────────────────────────────────────────┐
│ TimerBase                          "언제 울릴까"               │
│   최소힙 + 만료 실행 + 반복 재예약                              │
│   ├─ Timer      : 자체 스레드 + 세마포어 (코어 내장 폴백)       │
│   └─ LinuxTimer : timerfd + 이벤트 루프 (인프라, 프로덕션)      │
└───────────────────────────────────────────────────────────────┘
```
