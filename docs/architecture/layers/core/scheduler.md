# 스케줄러 — 액터 타이머 연결 계층

`startTimer()` 하나로 "지금부터 N ms 뒤에 이 액터에게 이 메시지를 전달"을 구현하는 과정과, 그 길에 있는 `Scheduler`와 `TimerBase`(`ITimer`)의 역할 분담을 처음 읽는 사람도 따라올 수 있게 정리한 문서.

> `startTimer`/`cancelTimer` API 사용법 자체는 [액터 모델](../../concepts/actor_model.md)의 "타이머" 절에서 다룹니다. OS 연동(timerfd vs std 스레드)은 [인프라 문서](../../concepts/infrastructure.md)를 참고하세요.

---

## 목차

- [개요 — 두 가지 관심사](#개요--두-가지-관심사)
- [역할 분담 — Scheduler vs TimerBase](#역할-분담--scheduler-vs-timerbase)
- [등록 흐름 — startTimer() 호출부터](#등록-흐름--starttimer-호출부터)
- [만료와 배달 — 시간이 되면](#만료와-배달--시간이-되면)
  - [왜 clone()인가](#왜-clone인가)
  - [콜백의 안전 설계](#콜백의-안전-설계)
- [취소와 정리](#취소와-정리)
- [종료 순서](#종료-순서)
- [전체 구조도](#전체-구조도)

---

## 개요 — 두 가지 관심사

"나중에 이 액터에게 이 메시지를 전달해 달라"는 요구 한 줄에는 사실 서로 다른 두 관심사가 들어 있습니다:

1. **언제 만료될까** — 지연 시간 관리, 최소힙, 만료 판단, OS 웨이크업
2. **누구에게 무엇을 전달할까** — 만료 시점에 어느 액터의 메일박스에 어떤 메시지를 넣을지

V² Engine은 이 둘을 별개 클래스로 분리했습니다:

```
Actor::startTimer(msg, delayMs, repeating)     [편의 API]
        │
ActorRuntime::addTimer()                       [소유 추적 + 위임]
        │
Scheduler (src/core/actor_system/runtime/scheduler/)     ★ "누구에게 무엇을"
        │ ITimer 포트 경유
TimerBase / LinuxTimer / Timer                 ★ "언제 만료될까"
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

Scheduler는 생성자에서 `ITimer`를 주입받고, 주입되지 않으면 표준 스레드 기반 `Timer`를 폴백으로 사용합니다:

```cpp
Scheduler::Scheduler(std::unique_ptr<ITimer> timer){
    timer_ = timer ? std::move(timer) : std::make_unique<Timer>();
}
```

덕분에 인프라가 없어도(단위 테스트 등) 코어만으로 타이머가 동작하고, 프로덕션에서는 `LinuxTimer`(timerfd)가 꽂혀 전용 스레드 없이 이벤트 루프와 함께 동작합니다([infrastructure.md](../../concepts/infrastructure.md)).

---

## 등록 흐름 — startTimer() 호출부터

액터가 `startTimer(Tick{}, 1000, true)`를 호출하면 세 층을 차례로 통과합니다:

```
① Actor::startTimer()
      └─ runtime()->addTimer(this, msg, delay, repeating)

② ActorRuntime::addTimer()                    [actor_runtime.cpp]
      ├─ timerMutex_ 잠금                     ← 내 타이머 ID 목록 보호
      └─ scheduler_->addTimer(targetRuntime, msg, delay, repeating)
         성공하면 timerIds_에 id 기록          (소멸 때 자동 정리용)

③ Scheduler::addTimer()
      ├─ mutex_ 잠금
      ├─ cleanupTimerCtxs()                   ← 죽은 ctx 청소 (아래 '취소와 정리')
      ├─ TimerCtx{target, msg} 생성
      ├─ id = timer_->add(delayMs, repeating, timerCallback, this)
      │                        ▲               ▲
      │              만료 시 불릴 함수   자기 자신을 컨텍스트로
      └─ timerCtxs_[id] = ctx
```

핵심은 **각 층이 자기 데이터만 소유한다**는 점입니다:
- ActorRuntime은 `timerIds_` (내가 건 타이머 목록 → 소멸자 정리용)
- Scheduler는 `timerCtxs_` (id가 누구에게 무엇을 보낼지)
- TimerBase는 최소힙 노드 (언제 만료될지만)

---

## 만료와 발화 — 시간이 되면

TimerBase가 만료를 감지하면 등록 때 넘겨준 콜백을 호출합니다. 어떤 구현(스레드 폴백이든 timerfd든)이든 이 공통 로직은 `handleTimerEvent()` 하나로 처리됩니다:

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

발화 경로는 특별하지 않습니다. 평범한 `enqueue()` 한 번일 뿐이라, 타이머 메시지도 [메시지 파이프라인](../../concepts/messaging.md)(메일박스 push → 디스패치 → 워커 → handle)을 그대로 거치며 대상 액터의 홈 워커에서 순서대로 처리됩니다.

### 왜 clone()인가

```cpp
target->enqueue(ctx->msg.clone());    // 원본이 아니라 '복제본'을 배달
```

원본(`ctx->msg`)은 다음 만료를 위해 Scheduler가 계속 보관해야 합니다. 반복 타이머는 같은 메시지를 여러 번 전달해야 하는데, 원본을 move로 넘겨버리면 두 번째 만료부터 빈 메시지가 전달되는 버그가 됩니다. 그래서 **만료 때마다 복제본**을 만들어 배달합니다 ([messaging.md](../../concepts/messaging.md)).

> 이 선택에는 제약 하나가 따릅니다 — 복사 불가 타입은 clone() 결과가 빈 메시지가 되므로, 타이머에 쓸 메시지 타입은 복사 가능해야 합니다.

### 콜백의 안전 설계

타이머 콜백은 전용 스레드(LinuxTimer라면 이벤트 루프 스레드)에서 실행됩니다. 여기서 예외가 밖으로 새면 **타이머 기능 전체가 멈추므로**, 콜백은 세 겹의 방어를 둡니다:

1. **취소된 id 무시** — ctx 조회 실패는 조용히 return. cancel과 만료 처리가 겹치는 순간에도 이미 삭제된 ctx 접근 크래시가 나지 않습니다.
2. **예외 완전 격리** — `enqueue()`가 던질 수 있는 예외를 잡아 로그만 남깁니다.
3. **단일 락 진입** — 등록·취소·만료 처리가 모두 `mutex_`를 통과하므로 ctx 맵은 항상 일관된 상태를 유지합니다.

---

## 취소와 정리

**명시적 취소** — 액터의 `cancelTimer(id)`가 결국 `Scheduler::cancel(id)`를 호출합니다:

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
        it = timerCtxs_.erase(it);     // 만료를 마친 1회성 타이머
    else
        ++it;
}
```

1회성(non-repeating) 타이머는 만료 후 힙에서는 사라져도 ctx 맵에는 그대로 남습니다. 등록 때마다 살아있는 타이머만 골라 치워 맵이 무한히 커지지 않게 합니다.

**액터 소멸 시 자동 정리** — `ActorRuntime` 소멸자는 `timerIds_`의 모든 id를 `scheduler_->cancel()`로 정리합니다. 액터가 사라진 뒤에도 타이머가 남아 죽은 액터에게 메시지를 보내는 일은 구조적으로 불가능합니다([actor_system.md](actor_system.md)).

---

## 종료 순서

```cpp
void Scheduler::stop(){
    timer_->stop();            // ① 먼저 타이머를 멈춘다 (새 만료 차단)
    std::lock_guard lock(mutex_);
    timerCtxs_.clear();        // ② 그다음 ctx를 지운다
}
```

순서가 바뀌면 위험합니다. ctx를 먼저 지우는 순간, 아직 실행 중인 콜백이 해제된 ctx를 건드릴 수 있습니다. 반대로 **타이머를 먼저 멈추면** 이후 콜백이 아예 호출되지 않으므로 안전하게 비울 수 있습니다. `ActorSystem::stop()`이 스케줄러를 가장 먼저 끄는 것도 같은 이유입니다([actor_system.md](actor_system.md)).

---
