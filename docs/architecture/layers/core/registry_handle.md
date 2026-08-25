# 레지스트리와 세대 기반 핸들

`ActorRegistry`(이름/ID 조회소)와 `ActorHandle`(만료를 감지하는 안전 참조)이
어떻게 **use-after-free 없이** 느슨하게 액터를 가리키는지 처음 읽는 사람도
따라올 수 있게 정리한 문서.

> [액터 모델](../../concepts/actor_model.md)에서 이 두 클래스의 API와
> "세대 카운터를 쓴다"는 사실을 소개했습니다. 이 문서는 그 **메커니즘의 실제
> 동작** — 세대 번호가 언제 오르고, 누가 검증하고, 충돌 이름은 어떻게
> 되는지 — 를 다룹니다.

---

## 목차

- [개요 — 왜 포인터를 그냥 쓰면 안 되나](#개요--왜-포인터를-그냥-쓰면-안-되나)
- [데이터 구조](#데이터-구조)
- [세대(generation) 메커니즘](#세대generation-메커니즘)
  - [등록 시 — add()](#등록-시--add)
  - [해제 시 — remove()](#해제-시--remove)
  - [검증 시 — resolve()](#검증-시--resolve)
- [이름 충돌 규칙](#이름-충돌-규칙)
- [forEachActor — 스냅샷 패턴](#foreachactor--스냅샷-패턴)
- [ActorHandle 내부](#actorhandle-내부)
- [수명 시나리오 따라가기](#수명-시나리오-따라가기)
- [동시성 — shared_mutex](#동시성--shared_mutex)
- [요약](#요약)

---

## 개요 — 왜 포인터를 그냥 쓰면 안 되나

액터들은 서로를 "이름"으로 부릅니다: `sendMsg("monitor", msg)`.
내부적으로는 결국 액터 객체의 원시 포인터(`Actor*`)를 얻어야 하는데,
여기에 함정이 있습니다:

```
t0: Actor* a = registry.find("worker");     // 포인터 획득
t1: (어딘가에서 worker 액터 제거 → delete)   // 포인터가 매달림!
t2: a->sendMsg(...);                        // 💀 use-after-free
```

`shared_ptr`로 해결할 수도 있지만, 액터 시스템 전체가 소유권을 공유하면
수명 관리가 오히려 꼬입니다(순환 참조, 종료 순서 문제). V² Engine의 답:

- 소유권은 **ActorSystem만** 독점합니다 (`unique_ptr`)
- 다른 모든 참조는 **핸들(id + 세대 번호)** 로, 사용할 때마다 유효성을 재확인합니다

죽은 액터를 가리키는 핸들은 nullptr을 돌려줄 뿐, 크래시가 나지 않습니다.

---

## 데이터 구조

**파일:** `src/core/actor_system/actor/actor_registry.hpp`

```cpp
struct ActorEntry{
    Actor* actor;           // 조회된 원시 포인터 (소유권 없음)
    uint64_t generation;    // 이 등록의 세대 번호
};

mutable std::shared_mutex mutex_;
std::unordered_map<std::string, ActorEntry> byName_;    // 이름 → 엔트리
std::unordered_map<uint64_t, ActorEntry> byId_;         // ID → 엔트리
std::unordered_map<uint64_t, uint64_t> generations_;    // ID → 다음 세대 번호
```

두 개의 인덱스 맵(byName/byId)으로 이름·ID 어느 쪽이든 O(1) 조회가 가능하고,
`generations_` 맵은 **ID 슬롯별로 몇 대의 액터가 지나갔는지**를 셉니다.
이것이 만료 감지의 핵심 재료입니다.

---

## 세대(generation) 메커니즘

### 등록 시 — add()

```cpp
void ActorRegistry::add(Actor* actor){
    std::unique_lock lock(mutex_);
    uint64_t id = actor->id();
    uint64_t gen = generations_[id]++;   // ★ 현재 값 받아오고 +1
    ...
    byName_[actor->name()] = {actor, gen};
    byId_[id] = {actor, gen};
    actor->setGeneration(gen);
}
```

`gen = generations_[id]++`는 후위 증가 — **현재 값을 받아온 뒤** 테이블을
올립니다. 첫 등록이라면 gen=0, 같은 ID로 두 번째 등록이면 gen=1...

### 해제 시 — remove()

```cpp
void ActorRegistry::remove(Actor* actor){
    std::unique_lock lock(mutex_);
    uint64_t id = actor->id();
    generations_[id]++;                  // ★ 세대 증가 → 옛 핸들 일괄 무효화
    auto nameIt = byName_.find(actor->name());
    if((nameIt != byName_.end()) && (nameIt->second.actor == actor)){
        byName_.erase(nameIt);           // 이름은 조건부 삭제 (§이름 충돌)
    }
    byId_.erase(id);
}
```

remove의 본질은 맵 삭제보다 **`generations_[id]++` 한 줄**입니다.
이 순간부터 그 ID를 가리키던 *모든* 옛 핸들의 세대 번호는 낡은 값이 됩니다.

### 검증 시 — resolve()

```cpp
Actor* ActorRegistry::resolve(const ActorHandle& handle) const {
    std::shared_lock lock(mutex_);
    auto it = byId_.find(handle.id());
    if(it == byId_.end()) return nullptr;                    // ① 아예 없음
    if(it->second.generation != handle.generation())
        return nullptr;                                      // ② 세대 불일치 = 낡은 핸들
    return it->second.actor;                                 // ③ 유효
}
```

핸들을 쓸 때마다 resolve가 이렇게 검사하므로:

| 상황 | 결과 |
|------|------|
| 액터가 살아있음 | 정상 포인터 반환 |
| 액터가 제거됨 | nullptr |
| **그 ID 자리에 다른 액터가 다시 등록됨** | **nullptr** ← 포인터 방식이었다면 새 액터를 잘못 건드렸을 상황 |

세 번째 행이 세대 장치의 존재 이유입니다. 현재 구현에서는 액터 ID가
단조 증가(`nextActorId_++`)해서 런타임 중 재활용이 없으므로, 세대
검사는 사실상 **이중 안전장치**입니다. 하지만 나중에 ID 풀링·재활용이
도입되더라도 옛 핸들은 세대 번호 하나로 걸러지므로 코드 변경 없이
안전합니다 — "지금은 필요 없어 보여도 소유권 경계에는 넣어두는"
방어적 설계의 좋은 예입니다.

---

## 이름 충돌 규칙

이름과 ID의 삭제 정책은 의도적으로 다릅니다.

**add에서 충돌** — 다른 액터가 이미 그 이름을 쓰고 있으면:

```cpp
if(nameIt != byName_.end() && (nameIt->second.actor != actor))
    V2_LOG_ERROR("Actor name '{}' already registered ... overwriting ...");
byName_[actor->name()] = {actor, gen};   // 경고 후 덮어씀
```

개발자 실수(중복 이름)를 치명적으로 만들지 않고 로그로 알려준 뒤 최신 등록이
이깁니다. ID 축은 항상 고유하므로 영향이 없습니다.

**remove에서 보호** — 이름 엔트리를 지우는 것은 *엔트리 주인이 나일 때만*:

```cpp
if((nameIt != byName_.end()) && (nameIt->second.actor == actor))
    byName_.erase(nameIt);
```

A가 제거된 직후 B가 같은 이름 "sensor"를 등록했다면, 나중에 A가 제거될 때
B의 이름 엔트리를 함께 지워버리는 사고를 막는 조건입니다.

---

## forEachActor — 스냅샷 패턴

전체 액터 순회(OneForAll 브로드캐스트, CLI 목록 출력 등)는 이렇게 동작합니다:

```cpp
void ActorRegistry::forEachActor(const std::function<void(ActorHandle)>& cb) const {
    std::vector<ActorHandle> snapshot;
    {
        std::shared_lock lock(mutex_);          // ① 짧게 락 걸고
        for(auto& [id, entry] : byId_)
            snapshot.emplace_back(id, entry.generation, this);  // 핸들만 복사
    }                                           // ② 여기서 락 해제!
    for(const auto& handle : snapshot)
        callback(handle);                       // ③ 락 없이 콜백 실행
}
```

콜백 안에서 액터가 메시지를 보내거나 심지어 제거되어도 데드락이 나지 않습니다.
순회 도중 제거된 액터의 핸들은 resolve에서 nullptr을 줄 뿐입니다.
OneForAll 배선([actor_system.md](actor_system.md))이 바로 이 점에 의존합니다.

---

## ActorHandle 내부

**파일:** `src/core/actor_system/actor/actor_handle.hpp`, `.cpp`

```cpp
class ActorHandle{
    uint64_t id_;                        // 누구를
    uint64_t generation_;                // 몇 대째를
    const IActorRegistry* registry_;     // 어디서 확인할지
};
```

| 메서드 | 동작 |
|--------|------|
| `get()` | `registry_->resolve(*this)` — 세대 검증 후 포인터 또는 nullptr |
| `valid()` | `get() != nullptr` — 편의 검사 |
| `send(msg)` | get() 성공 시 전달; 실패 시 **경고 로그 + 드롭** |

```cpp
void ActorHandle::send(Message msg) const {
    Actor* actor = get();
    if(actor){
        actor->receiveMsg(std::move(msg));
    }else{
        V2_LOG_WARN("ActorHandle target id={} (gen={}) not found, dropping ...");
    }
}
```

템플릿 오버로드 덕분에 임의 타입도 바로 보낼 수 있습니다:

```cpp
handle.send(Tick{});        // 자동으로 Message::make(Tick{}) 변환
```

핸들은 복사 가능한 작은 구조체(16바이트 + 포인터)라서 컨테이너에 담거나
마음껏 전달해도 안전합니다.

---

## 수명 시나리오 따라가기

```
t0  createActor<MonitorActor>("monitor")      id=3, generations_[3]: 0→1, gen=1 부여
t1  h = findHandleByName("monitor")            h = {id:3, gen:1}  ✓ 유효
t2  h.send(MonitorSubscribe{...})              resolve OK → 전달
t3  (모니터 액터 제거)                          remove → generations_[3]: 1→2
t4  h.send(...)                                resolve 실패 (1≠2) → nullptr
                                               → 경고 로그 + 드롭, 크래시 없음
```

t4가 이 장치의 일상적인 가치입니다. 핸들만 들고 있다면 그 액터가
언제 제거됐는지 알 방법이 없지만, 세대 번호 하나로 안전하게 판별됩니다.
(현재 구조에서 ID가 재활용되지 않으므로 t4 이후 같은 ID로 새 액터가
등록되는 일은 없습니다 — 그 경우까지 겨냥한 설계라는 것은 위의
"이중 안전장치" 설명을 참고하세요.)

---

## 동시성 — shared_mutex

레지스트리는 읽기가 압도적으로 많습니다(매 sendMsg마다 조회). 그래서:

| 연산 | 락 | 동시성 |
|------|-----|--------|
| find*/resolve/forEachActor 스냅샷 | `shared_lock` (읽기) | **여러 스레드 동시 통과** |
| add/remove/clear | `unique_lock` (쓰기) | 단독 접근 |

읽기-읽기 경쟁이 없으므로 워커들이 동시에 다른 액터를 조회해도 서로
기다리지 않습니다. 쓰기(add/remove)는 액터 생성·소멸 때만 일어나는
드문 연산입니다. 뮤텍스 사용 경계 전체 목록은
[동시성 문서](../../concepts/concurrency.md)의 표를 참고하세요.

---

## 요약

| 항목 | 설명 |
|------|------|
| **문제** | 제거된 액터를 향한 원시 포인터 참조 = use-after-free |
| **해법** | 소유권은 System 독점, 참조는 핸들(id+세대)+사용 시점 검증 |
| **세대 증가 시점** | add(현재 값 부여) / remove(+1 → 옛 핸들 일괄 무효화) |
| **resolve 3단계** | 존재? → 세대 일치? → 포인터 반환 |
| **이름 규칙** | add 충돌은 경고 후 덮어씀 / remove는 주인일 때만 삭제 |
| **forEachActor** | 핸들 스냅샷 후 락 해제 — 콜백 중 데드락 없음 |
| **핸들 send** | 만료 시 경고 로그+드롭 — 조용하지만 안전한 실패 |
| **동시성** | shared_mutex — 조회 병렬, 수정 독점 |
