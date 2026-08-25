# 액터 시스템 — 조립과 생명주기

`ActorSystem`이 의존성들을 어떻게 조립하고, 시스템 전체를 어떤 순서로
켜고 끄는지, 그리고 `ActorRuntime`이 어떻게 실패에 견고하게 메시지를
처리하는지 처음 읽는 사람도 따라올 수 있게 정리한 문서.

> 액터의 개념과 API는 [액터 모델](../../concepts/actor_model.md)을,
> 실행 토큰·디스패처 내부는 [작업 분배](../../concepts/work_dispatch.md)을 먼저
> 보면 이해가 빠릅니다.

---

## 목차

- [개요 — ActorSystem의 두 얼굴](#개요--actorsystem의-두-얼굴)
- [컴포넌트 지도](#컴포넌트-지도)
- [조립 — createDefaultActorSystem](#조립--createdefaultactorsystem)
- [액터 등록 — attachActor](#액터-등록--attachactor)
- [기동 시퀀스 — start()](#기동-시퀀스--start)
- [종료 시퀀스 — stop()](#종료-시퀀스--stop)
- [OneForAll 콜백 배선](#oneforall-콜백-배선)
- [ActorRuntime의 견고성](#actorruntime의-견고성)
  - [메일박스 가득 참 — 드롭 정책](#메일박스-가득-참--드롭-정책)
  - [processBatch — 예외를 슈퍼바이저로](#processbatch--예외를-슈퍼바이저로)
  - [performRestart — 절반쯤 죽은 액터 되살리기](#performrestart--절반쯤-죽은-액터-되살리기)
- [요약](#요약)

---

## 개요 — ActorSystem의 두 얼굴

**파일:** `src/core/actor_system/actor_system.hpp`, `.cpp`

`ActorSystem`은 두 가지 역할을 동시에 합니다:

1. **조립체(composition)** — 슈퍼바이저, 디스패처, 스케줄러, 레지스트리,
   데드 레터 큐, 워커들을 한데 묶고 서로를 연결합니다.
2. **수명 관리자(lifecycle owner)** — 모든 하위 컴포넌트와 액터의
   시작/종료 순서를 책임집니다.

특별한 점은 **코어 안에서도 의존성 주입이 적용된다**는 것입니다.
`ActorSystem`은 구체 클래스를 직접 만들지 않고, `ActorSystemDeps` 구조체로
인터페이스 포인터들을 받습니다. 진짜 구현체를 만들어 꽂아주는 건 팩토리 함수
`createDefaultActorSystem()`입니다.

---

## 컴포넌트 지도

`src/core/actor_system/` 폴더의 파일별 역할과 심층 문서 위치:

| 파일/폴더 | 역할 | 심층 문서 |
|-----------|------|-----------|
| `actor_system.hpp/cpp` | 조립 + 기동/종료 순서 | **이 문서** |
| `actor/actor.cpp` | 액터 기본 클래스, dispatch | [concepts/actor_model.md](../../concepts/actor_model.md) |
| `actor/actor_registry.cpp` | 이름/ID 조회 + 세대 검증 | [registry_handle.md](registry_handle.md) |
| `actor/actor_handle.cpp` | 만료 감지 참조 | [registry_handle.md](registry_handle.md) |
| `messages/` | Message 본체 + 40개 ID | [concepts/messaging.md](../../concepts/messaging.md) |
| `runtime/actor_runtime.cpp` | 배치 처리, 재시작, 타이머 위임 | **이 문서** §견고성 |
| `runtime/dispatcher/` | 실행 토큰, 작업 스틸링, 워커 | [concepts/work_dispatch.md](../../concepts/work_dispatch.md), [concurrency.md](../../concepts/concurrency.md) |
| `runtime/mailbox/mailbox.hpp` | MPSC 큐 얇은 래퍼 (21줄) | 알고리즘은 [concurrency.md](../../concepts/concurrency.md) |
| `runtime/scheduler.cpp` | 액터 타이머 연결 | [scheduler.md](scheduler.md) |
| `runtime/supervisor/` | 재시작 정책, 데드 레터 | [concepts/supervision.md](../../concepts/supervision.md) |

---

## 조립 — createDefaultActorSystem

**파일:** `actor_system.cpp:116`

팩토리 함수가 만드는 것들의 의존 관계:

```
DeadLetterQueue (용량 설정값)
    │ 참조
    ▼
Supervisor (maxRestarts, 기본 전략 주입)
Registry (비어있음)

WorkDispatcher (워커 수, 큐 용량, 하이워터마크, 백오프·스핀 파라미터)
Scheduler (ITimer 주입 — 없으면 std 스레드 Timer 폴백)

        └── 모두 ActorSystemDeps에 담겨 ──► ActorSystem(config, deps)
```

주목할 디테일 하나 — **하이워터마크 자동 계산**:

```cpp
int highWatermark = (config.dispatcherHighWatermark > 0)
    ? config.dispatcherHighWatermark
    : (config.dispatcherQueueCapacity * 7 / 10);   // 기본값 = 용량의 70%
```

설정에서 따로 주지 않으면 큐 용량의 70%를 홈 워커 포화 판단선으로 삼습니다
([부하 인식 디스패치](../../concepts/concurrency.md)에서 쓰이는 값).

`Worker` 스레드는 생성자에서 미리 만들어지지만(`workers_.reserve` 후 push),
**`start()` 전까지는 대기 상태**입니다. 객체 생성과 스레드 기동이 분리되어 있어
조립 단계에서는 아무것도 달리지 않습니다.

---

## 액터 등록 — attachActor

`createActor<T>()`(헤더의 템플릿 팩토리)가 호출하는 내부 경로입니다:

```cpp
void ActorSystem::attachActor(std::unique_ptr<Actor> actor,
                              size_t mailboxSize, uint64_t id){
    auto mailbox = std::make_unique<Mailbox>(mailboxSize);
    auto rt = std::make_unique<ActorRuntime>(
        std::move(actor), std::move(mailbox),
        dispatcher_.get(), scheduler_.get(),
        registry_.get(), eventLoop_.get(), supervisor_.get());
    registry_->add(rt->actor());          // 이름/ID/세대 등록
    actorRuntimes_.push_back(std::move(rt));
    V2_METRICS()->registerActor(id);      // 메트릭 슬롯 확보
}
```

여기서 `Mailbox`는 락프리 MPSC 큐(`LockFreeMpscQueue<Message>`)를 감싼
얇은 래퍼일 뿐입니다 — push/pop/count/clear 전부 큐에 위임합니다.

`ActorRuntime` 생성자가 액터가 시스템과 통신하는 데 필요한 **7개 의존성**
(디스패처, 스케줄러, 레지스트리, 이벤트 루프, 슈퍼바이저)을 포인터로 받아
`actor_->setRuntime(this)`로 액터와 서로 연결합니다. 액터의 `sendMsg()`가
동작할 수 있는 이유가 이 연결입니다.

---

## 기동 시퀀스 — start()

```cpp
void ActorSystem::start(){
    dispatcher_->start();            // ① 디스패처 (워커 큐 준비)
    if(eventLoop_) eventLoop_->start(); // ② 이벤트 루프 (epoll/eventfd 준비)
    scheduler_->start();             // ③ 스케줄러 (타이머 기반 가동)
    for(auto& ctx : actorRuntimes_)
        ctx->actor()->open();        // ④ 모든 액터 open (실패는 로그만, 계속)
    for(auto& w : workers_)
        w->start();                  // ⑤ 마지막에 워커 스레드 가동
}
```

순서의 의미:

- **워커를 마지막에** 띄웁니다. `open()` 중에 다른 액터로 메시지를 보내면
  메일박스엔 쌓이지만, 워커가 없으니 아무도 처리하지 않습니다 — 워커가
  뜨는 순간 일괄 처리됩니다. 액터들이 "완전히 준비된 상태에서" 일을
  받기 시작한다는 보장이 됩니다.
- **개별 `open()` 실패는 치명적이지 않습니다.** 에러 로그를 남기고 다음 액터로
  넘어갑니다. 한 액터 초기화 실패로 데몬 전체가 안 뜨는 것을 막습니다.
- 전체 기동 중 **예외가 튀면** catch해서 `stop()`으로 절반쯤 켜진 시스템을
  깔끔히 끈 뒤 예외를 다시 던집니다(rollback).

### run()과 requestStop()

```cpp
void ActorSystem::run(){ if(eventLoop_) eventLoop_->run(); }
       // 메인 스레드가 이벤트 루프에 블록 — 사실상 프로세스의 심장박동
void ActorSystem::requestStop(){ if(eventLoop_) eventLoop_->stop(); }
       // SIGINT/SIGTERM 핸들러에서 부름 — 루프를 깨워 종료 흐름 진입
```

`run()`은 이벤트 루프([인프라](../../concepts/infrastructure.md))가
`epoll_wait`에서 잠드는 것이 곧 프로세스 전체의 대기 상태입니다.

---

## 종료 시퀀스 — stop()

```cpp
void ActorSystem::stop(){
    scheduler_->stop();              // ① 새 타이머 발화 차단
    if(eventLoop_) eventLoop_->stop();// ② 새 I/O 이벤트 차단
    dispatcher_->beginDrain();       // ③ "남은 토큰 다 처리해라" 선언
    for(auto& w : workers_)
        w->stop();                   // ④ 워커들이 drain 완료 후 종료 (join)
    dispatcher_->stop();             // ⑤ 디스패처 정리
    for(auto& ctx : actorRuntimes_)
        ctx->actor()->close();       // ⑥ 모든 액터 close
}
```

기동의 역순이면서도 핵심 규칙이 있습니다: **새 작업 유입을 먼저 차단하고
(①②③), 이미 들어온 작업은 끝까지 처리하게 한 뒤(④), 마지막에 액터를
닫는다(⑥).**

- ③ `beginDrain()`과 ④의 관계 — 드레인 프로토콜 상세는
  [동시성 문서 §드레인](../../concepts/concurrency.md). `pendingWork_`가
  0이 될 때까지 워커가 버티므로, 메일박스에 밀린 메시지가 조용히 버려지지 않습니다.
- 소멸자(~ActorSystem)도 `stop()` + `registry_->clear()`를 호출하므로,
  stop을 깜빡해도 안전합니다.

---

## OneForAll 콜백 배선

생성자에서 슈퍼바이저가 쓸 **전체 재시작 콜백**을 연결합니다:

```cpp
supervisor_->setRestartAll([this]() -> int {
    int count = 0;
    registry_->forEachActor([&](ActorHandle h){
        Actor* a = h.get();
        if(!a || !a->runtime()) return;
        ActorRestartRequest req;
        req.reason = "one-for-all restart";
        a->runtime()->enqueue(Message::make(std::move(req)));
        ++count;
    });
    return count;
});
```

재시작도 특별한 명령이 아니라 **메시지**(`ActorRestartRequest`)로 이뤄집니다.
각 액터는 자기 홈 워커에서 이 메시지를 받아 스스로 재시작하므로, 슈퍼바이저가
다른 워커를 건드릴 일이 없습니다([supervision.md](../../concepts/supervision.md)).
`forEachActor`의 스냅샷 동작은 [registry_handle.md](registry_handle.md)에서 다룹니다.

---

## ActorRuntime의 견고성

**파일:** `runtime/actor_runtime/actor_runtime.cpp`

`ActorRuntime`은 액터를 감싸는 방탄 유리 케이스입니다. 액터 코드가 멋대로
예외를 던져도 시스템이 버티도록 하는 장치들이 여기 있습니다.

### 메일박스 가득 참 — 드롭 정책

```cpp
void ActorRuntime::enqueue(Message msg){
    if(!mailbox_->push(std::move(msg))){
        V2_METRICS()->recordEnqueue(actor_->id(), false, 0);
        V2_LOG_WARN("Actor {} mailbox full, dropping message ...");
        return;                       // ★ 블로킹하지 않고 드롭
    }
    ...
    if(workDispatcher_) workDispatcher_->dispatch(this);
}
```

느린 소비자 때문에 생산자(워커 전체!)가 멈추는 백프레셔 역류를 막기 위해,
**전달 보장보다 가용성을 택해** 가득 찬 메일박스엔 드롭+경고를 선택합니다.
드롭 수는 `dropped` 메트릭([metrics.md](metrics.md))으로 집계됩니다.

### processBatch — 예외를 슈퍼바이저로

```cpp
try{
    if(!tryConsumeLifecycle(msg))     // 시스템 메시지 먼저 가로채기
        actor_->handle(msg);
}catch(const std::exception& e){
    supervisor_->onActorFailed(this, std::move(msg), e.what());
    break;                            // ★ 이 배치는 여기서 중단
}
```

액터 핸들러의 예외는 **잡혀서 실패 메시지와 함께 슈퍼바이저에 보고**되고,
현재 배치는 중단됩니다. 워커 스레드는 살아있으므로 다른 액터는 계속
처리됩니다 — 실패의 격리. 슈퍼바이저가 그 뒤를 어떻게 처리하는지는
[supervision.md](../../concepts/supervision.md)의 3단계 흐름입니다.

### performRestart — 절반쯤 죽은 액터 되살리기

재시작 자체가 또 실패할 수 있다는 것을 고려한 방어 코드입니다:

```
1. actor_->close() 시도
   └─ close()가 예외를 던져도 기록만 하고 계속 진행
2. 상태가 Closed가 되었으면 actor_->open() 시도
   └─ open()마저 실패하면 supervisor_->onActorFailed(...)로 재보고
      → 슈퍼바이저의 예산(maxRestarts)이 재시작 루프를 끊어줌
```

`close()` 실패 후 상태가 Closed가 아니면 open을 시도하지 않아,
"깨진 상태에서 무작정 재가동"하는 최악의 경우를 피합니다. 재시작이
연속 실패하면 슈퍼바이저의 재시작 예산이 소진되어 영구 종료로
수렴합니다 — 무한 좀비 루프의 방지책입니다.

참고: OneForAll 브로드캐스트로 인한 재시작(`tryConsumeLifecycle`의
`ActorRestartRequest` 처리)은 Closed 상태의 액터를 건너뛰고 Opened 액터만
재시작합니다 — 종료 중인 시스템을 다시 켜는 사고를 막는 가드입니다.

---

## 요약

| 항목 | 설명 |
|------|------|
| **역할** | 컴포넌트 조립 + 전체 수명 관리 |
| **조립** | `createDefaultActorSystem()`이 포트 구현체를 만들어 `ActorSystemDeps`로 주입 |
| **기동 순서** | 디스패처 → 이벤트 루프 → 스케줄러 → 액터 open() → 워커 (워커가 마지막) |
| **종료 순서** | 타이머/I/O 차단 → 드레인 → 워커 join → 액터 close (유입 차단 먼저) |
| **open 실패** | 로그 후 계속 — 단일 액터 실패가 시스템 기동을 막지 않음 |
| **메일박스 가득** | 블로킹 없이 드롭+경고+메트릭 (가용성 우선) |
| **핸들러 예외** | 슈퍼바이저 보고 후 배치 중단 — 실패는 해당 액터에만 격리 |
| **재시작 실패** | close/open 각각 방어, open 실패 시 재보고 → 예산으로 수렴 |
