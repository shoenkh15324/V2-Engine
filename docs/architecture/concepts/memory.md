# 메모리 관리

V² Engine이 메시지와 객체를 어떻게 할당하고 해제하는지, 그리고 왜 일반 `malloc`/`new` 대신
직접 만든 메모리 풀을 쓰는지 처음 읽는 사람도 따라올 수 있게 정리한 문서.

---

## 목차

- [개요](#개요)
- [설계 원칙](#설계-원칙)
- [전체 그림 — 3계층 아키텍처](#전체-그림--3계층-아키텍처)
- [SizeClass — 크기별 반찬통](#sizeclass--크기별-반찬통)
- [FreeList — 공짜 블록 줄 세우기](#freelist--공짜-블록-줄-세우기)
- [ThreadLocalCache — 1계층: 내 책상 서랍](#threadlocalcache--1계층-내-책상-서랍)
- [CentralCache — 2계층: 층별 대출 데스크](#centralcache--2계층-층별-대출-데스크)
- [Slab — 3계층: 원재료 덩어리](#slab--3계층-원재료-덩어리)
- [MemoryPoolT — 조립 완제품](#memorypoolt--조립-완제품)
- [메시지 시스템과의 연계](#메시지-시스템과의-연계)
- [통계와 디버깅](#통계와-디버깅)
- [설정](#설정)
- [요약](#요약)

---

## 개요

> 📌 이 할당자를 실제로 사용하는 주인공은 [Message](messaging.md)입니다. 64바이트가 넘는 큰 메시지가
> 힙 대신 이 풀에서 할당됩니다.

C++에서 `new`를 호출하면 운영체제(정확히는 범용 할당자, malloc)가 적절한 빈 메모리를 찾아줍니다.
범용 할당자는 "어떤 크기가 올지 모른다"는 가정으로 설계되었기 때문에:

- 크기를 기억해야 하므로 블록마다 **관리 오버헤드**(헤더)가 붙습니다
- 여러 스레드가 동시에 `new`를 부르면 내부 잠금으로 **경쟁**이 생깁니다
- 크기가 제각각이면 메모리에 구멍(**파편화**)이 생깁니다

그런데 V² Engine의 워크로드는 특별합니다. **대부분 작고(≤ 2KB), 아주 자주, 순간적으로 몰려서**
할당/해제됩니다. 메시지가 대표적입니다. 이런 패턴에는 "작은 크기 몇 개 등급으로만 나눠서,
스레드별로 미리 담아두고, 잠금 없이 꺼내 쓰는" 전용 할당자가 훨씬 빠릅니다.

Google의 TCMalloc이 바로 이 아이디어로 유명한데, V² Engine은 같은 발상을 엔진에 맞게
축소해서 구현한 것입니다.

**파일 위치:** `src/core/common/memory/` (코어 계층이라 외부 의존성 없음)

---

## 설계 원칙

| 원칙 | 설명 |
|------|------|
| **크기 등급(Size Class)** | 요청 크기를 9개 등급(8B~2048B)으로 반올림해 관리합니다. 같은 등급끼리는 완전히 동일한 방식으로 처리할 수 있습니다. |
| **스레드 로컬 우선** | 할당/해제는 기본적으로 자기 스레드 전용 캐시에서 끝냅니다. 잠금이 아예 필요 없습니다. |
| **배치(Batch) 이동** | 캐시가 비거나 넘칠 때는 블록을 하나씩이 아니라 묶음으로 옮겨 잠금 횟수를 최소화합니다. |
| **큰 할당은 위임** | 2048B 초과 또는 특수 정렬 요청은 무리하지 않고 `::operator new`에 그대로 위임합니다. |
| **정책 주입** | 디버그 동작(사용 후 덮어쓰기)과 실패 동작(예외 vs abort)을 템플릿 정책으로 교체할 수 있습니다. |

---

## 전체 그림 — 3계층 아키텍처

도서관에 비유하면 이해가 빠릅니다.

```
[내 책상 서랍]              [층별 대출 데스크]           [창고 팔레트]
ThreadLocalCache            CentralCache                 Slab
(잠금 없음, 스레드 전용)     (뮤텍스 1개, 크기 등급별)      (4KB 원시 메모리)
      │                            │                          │
      │ 서랍에 남으면               │ 팔레트가 부족하면          │
      └── 배치 반납 ──────────────►│── 새 팔레트 추가 ────────►│
      │                            │                          │
      ◄──────── 배치 리필 ─────────┤◄──────── 블록 공급 ───────┤
```

- **평소(99%)**: 서랍에서 꺼내고 서랍에 넣습니다 → **잠금 제로**
- **서랍이 빌 때**: 데스크에서 한 번에 묶음(배치)만큼 리필 → 드물게 잠금
- **서랍이 넘칠 때**: 묶음만큼 데스크에 반납 → 드물게 잠금
- **팔레트가 부족할 때**: 4KB짜리 새 Slab을 통째로 확보

할당 흐름을 코드 관점에서 보면:

```
MemoryPool::allocate(size, align)
    ├─ size ≤ 2048 && align ≤ max_align_t  →  ThreadLocalCache (락프리)
    │                                              ↓ 캐시 미스 시 배치 리필
    │                                          CentralCache (뮤텍스)
    │                                              ↓ 공간 부족 시
    │                                          Slab 4KB 신규 확보
    └─ 그 외                                 →  ::operator new (위임)
```

---

## SizeClass — 크기별 반찬통

**파일:** `src/core/common/memory/size_class.hpp`

요청 크기를 "어느 등급 통에 넣을까"로 바꿔주는 정적 테이블입니다. 9개 등급이 있으며,
풀이 다루는 최대 크기는 **2048바이트**입니다.

| 등급 | 블록 크기 | 배치 크기 | Slab 4KB 활용 |
|------|-----------|-----------|---------------|
| 0 | 8B | 64 | 512B |
| 1 | 16B | 64 | 1KB |
| 2 | 32B | 32 | 1KB |
| 3 | 64B | 32 | 2KB |
| 4 | 128B | 16 | 2KB |
| 5 | 256B | 16 | 4KB |
| 6 | 512B | 8 | 4KB |
| 7 | 1024B | 4 | 4KB |
| 8 | 2048B | 2 | 4KB |

- **블록 크기**: 이 등급의 통 하나 크기. 300바이트를 요청하면 512B 등급에 담깁니다(반올림).
- **배치 크기**: 캐시↔중앙 캐시 간에 한 번에 이동하는 블록 수. 작은 블록일수록 많이 옮깁니다.

```cpp
static std::size_t index(std::size_t size){
    for(std::size_t i = 0; i < kNumSizeClasses; ++i){
        if(size <= kTable[i].blockSize) return i;   // 첫 번째로 "들어가는" 등급
    }
}
```

`index()`는 선형 탐색이지만 테이블이 9칸뿐이라 CPU 캐시에 다 들어가 충분히 빠릅니다.

---

## FreeList — 공짜 블록 줄 세우기

**파일:** `src/core/common/memory/free_list.hpp`

"비어 있는 블록"들을 연결 리스트로 엮은 자료구조입니다. 마법은 이렇습니다 —
**빈 블록의 몸통 안에 다음 빈 블록의 주소를 직접 저장**합니다(intrusive 방식).

```
FreeList::push(ptr):
    node = (Node*)ptr
    node->next = head_     ← 블록 자기 자신의 첫 8바이트를 포인터로 사용
    head_ = node

FreeList::pop():
    node = head_
    head_ = node->next
    return node            ← 그 주소를 그대로 돌려줌
```

덕분에 빈 블록 관리에 메모리가 **전혀 추가로 들지 않고**, push/pop은 포인터 두 번 조작으로
끝납니다. 이 때문에 가장 작은 등급이 8바이트인 것입니다(`sizeof(Node*) == 8`, 64비트 기준).
8바이트 미만 요청은 8B 등급으로 반올림되므로 항상 노드를 저장할 공간이 있습니다.

---

## ThreadLocalCache — 1계층: 내 책상 서랍

**파일:** `src/core/common/memory/thread_local_cache.hpp`

```cpp
inline thread_local std::array<ThreadLocalCache, kMaxPools> poolCaches;
```

`thread_local` 변수라서 **스레드마다 자기 복사본**을 가집니다. 여러 풀(MemoryPool 인스턴스)이
공존할 수 있으므로 풀 ID별로 배열이 준비되어 있습니다(`kMaxPools = 16`).

**할당 (락프리 빠른 경로):**

```cpp
void* allocate(std::size_t size){
    std::size_t idx = SizeClass::index(size);
    auto& cache = caches_[idx];
    if(cache.freeList.count() > 0) return cache.freeList.pop();  // ★ 대부분 여기서 끝
    return fetchFromCentral(idx);                                // 서랍 빔 → 느린 경로
}
```

**해제 (락프리 빠른 경로):**

```cpp
void deallocate(void* ptr, std::size_t size){
    auto& cache = caches_[SizeClass::index(size)];
    cache.freeList.push(ptr);
    if(cache.freeList.count() > SizeClass::batchSize(idx)){
        returnToCentral(idx);   // 서랍 넘침 → 묶음 반납
    }
}
```

**느린 경로 (`fetchFromCentral`)** — 서랍이 비었을 때 중앙 캐시에서 **배치 크기만큼 한꺼번에**
가져옵니다. 하나 1개만 가져오면 곧 또 비겠지만, 64개를 가져오면 다음 63번은 잠금 없이
해결됩니다. 이것이 "배치 이동" 원칙입니다.

스레드가 종료될 때는 소멸자가 `drainAll()`로 서랍의 모든 블록을 중앙 캐시에 돌려놓아
누수처럼 보이는 일이 없게 합니다.

---

## CentralCache — 2계층: 층별 대출 데스크

**파일:** `src/core/common/memory/central_cache.hpp`

크기 등급별로 하나씩 존재하는 중앙 창고입니다. 여기서부터는 여러 스레드가 접근하므로
**뮤텍스 1개**로 보호하지만, 스레드 로컬 계층이 배치로만 드물게 들르기 때문에 경쟁이 거의 없습니다.

핵심 데이터 구조:

```cpp
std::mutex mutex_;
std::list<Slab*> partialSlabs_;                    // "남은 공간 있는" 팔레트 목록
std::vector<std::unique_ptr<Slab>> slabs_;         // 소유한 모든 팔레트
std::unordered_map<std::uintptr_t, Slab*> slabMap_; // 주소 → 팔레트 역방향 조회
```

**Slab 상태 전이** — 팔레트는 세 가지 상태를 갖고, `transitionState()`가 목록 멤버십을 관리합니다:

```
Empty (전부 비어있음) ⇄ Partial (일부 사용 중) ⇄ Full (전부 사용 중)
```

할당은 항상 `partialSlabs_.front()`에서 꺼냅니다. Partial 목록이 비면 새 Slab을 추가합니다.

**역방향 조회의 비밀** — `deallocate(ptr)`로 돌아온 포인터가 어느 Slab 소속인지 어떻게 알까?
Slab은 **4KB(2의 거듭제곱)로 정렬**되어 있으므로:

```cpp
Slab* findSlab(void* ptr){
    // ptr & ~0xFFF = ptr이 속한 4KB 페이지의 시작 주소
    auto it = slabMap_.find(reinterpret_cast<std::uintptr_t>(ptr) & slabMask_);
    return (it != slabMap_.end()) ? it->second : nullptr;
}
```

주소에 마스크 AND 한 번으로 O(1)에 소속 Slab을 찾습니다. 이것이 Slab 크기를 2의 거듭제곱으로
강제하는 이유입니다.

`fetchBatch()` / `returnBatch()`는 스레드 로컬 캐시와 묶음 단위로 거래하는 창구입니다.

---

## Slab — 3계층: 원재료 덩어리

**파일:** `src/core/common/memory/slab.hpp`

실제 메모리를 운영체제에서 받아오는 최하층입니다. 기본 크기는 **4096바이트(4KB)** —
리눅스 페이지 크기와 같아서 OS 입장에서도 효율적입니다.

```cpp
explicit Slab(std::size_t blockSize, std::size_t slabSize = kDefaultSlabSize)
    : slabSize_(slabSize), blockSize_(blockSize), totalBlocks_(slabSize / blockSize){
    if((slabSize & (slabSize - 1)) != 0) throw ...;   // 2의 거듭제곱 강제
    memory_ = static_cast<uint8_t*>(::operator new(slabSize_, std::align_val_t{slabSize_}));
    for(std::size_t i = 0; i < totalBlocks_; ++i){
        freeList_.push(memory_ + i * blockSize_);     // 생성 시 전체를 빈 목록에
    }
}
```

즉 Slab이란 **동일 크기 블록 N개를 자른 4KB 판**이고, 잘라놓은 블록을 전부 FreeList에
등록해둔 것입니다. `allocateBatch()`/`deallocateBatch()`로 묶음 출납을 지원하며,
`contains(ptr)`로 "이 주소가 진짜 내 블록인지"(정렬 검사 포함) 확인할 수 있어
디버깅에도 쓰입니다.

---

## MemoryPoolT — 조립 완제품

**파일:** `src/core/common/memory/memory_pool.hpp`

지금까지의 부품을 합쳐 `IMemoryAllocator` 인터페이스로 노출하는 파사드(facade) 클래스입니다.
두 가지 정책을 템플릿 인자로 받아 컴파일 타임에 동작을 고릅니다:

| 정책 | 종류 | 동작 |
|------|------|------|
| `NoDebugPolicy` (기본) | 디버그 | 아무것도 안 함 — 운영용 |
| `PoisonDebugPolicy` | 디버그 | 해제 시 블록을 `0xCD`로 덮어씀 — use-after-free 즉시 드러남 |
| `ThrowAllocPolicy` (기본) | 실패 | `std::bad_alloc` 예외 던짐 |
| `NoExceptAllocPolicy` | 실패 | 예외 금지 환경용 — `std::abort()` |

```cpp
using MemoryPool = MemoryPoolT<NoDebugPolicy, ThrowAllocPolicy>;       // 운영용
using DebugMemoryPool = MemoryPoolT<PoisonDebugPolicy, ThrowAllocPolicy>; // 디버그용
```

타입 기반 헬퍼도 제공합니다:

```cpp
template<typename T, typename... Args>
T* allocate(Args&&... args);        // 공간 확보 + placement-new까지

template<typename T>
void deallocate(T* ptr);            // 소멸자 호출 + 공간 반환
```

**전역 싱글턴** — 프로그램 전체에서 하나만 쓰는 기본 풀입니다:

```cpp
inline MemoryPool& defaultMemoryPool(){
    static MemoryPool pool(...);   // 최초 사용 시점에 설정 반영해 생성
    return pool;
}
```

`main_app.cpp:83`에서 기동 초기에 `initGlobalMemoryPoolConfig(cfg_.memorySlabSize,
cfg_.memoryMaxPoolAllocSize)`를 호출해 JSON 설정을 반영하므로, 싱글턴은 그 설정으로
태어납니다.

---

## 메시지 시스템과의 연계

[Message](messaging.md)는 64바이트 이하 페이로드를 객체 안에 인라인 저장하므로 힙이 필요
없지만, 그보다 큰 페이로드는 **Pool 모드**로 이 문서의 풀을 사용합니다:

```cpp
// message.hpp — Pool 모드 할당/해제
void* mem = alloc->allocate(sizeof(DT), alignof(DT));   // defaultMemoryPool()
msg.storage_.ptr = ::new(mem) DT(std::forward<T>(value));

// 소멸 시
ops_->destroy(data(), allocator_ ? allocator_ : &defaultMemoryPool());
```

메시지는 짧게 살았다 죽는 경우가 압도적이므로, 같은 크기 등급을 반복해서 대출/반납하게 됩니다.
스레드 로컬 서랍이 워커별로 있기 때문에 **워커 스레드에서 메시지를 만들고 버리는 과정에
잠금 경쟁이 전혀 없습니다.**

---

## 통계와 디버깅

`IMemoryAllocator` 인터페이스는 현재 사용량 조회를 제공합니다:

```cpp
pool.allocatedBlocks();   // 사용 중인 블록 수 (모든 등급 합산)
pool.allocatedBytes();    // 블록 수 × 등급 블록 크기
```

디버깅 팁:

- **use-after-free 의심**: `DebugMemoryPool`로 교체하면 해제된 블록이 `0xCD`로 덮여
  이미 해제된 메모리를 건드리는 코드가 금방 드러납니다.
- **누수/과다 보유**: `allocatedBytes()`가 계속 오르면 등급별로 어느 쪽이 새는지
  `centralCaches()[idx].allocatedBlocks()`로 좁힐 수 있습니다.

---

## 설정

`config/v2_main.json`(RuntimeConfig)에서 조정할 수 있는 관련 키:

| 키 | 기본값 | 설명 |
|----|--------|------|
| `memory_slab_size` | 4096 | Slab 한 장 크기 (2의 거듭제곱 필수) |
| `memory_max_pool_alloc_size` | 2048 | 이 값 이하는 풀이 처리, 초과는 `operator new` 위임 |

> 참고: 설정 로더는 `memory_max_pools`, `memory_tls_batch_size` 키도 읽지만,
> 현재 구현에서 `kMaxPools`(16)와 배치 크기는 `SizeClass` 테이블의 컴파일 타임 상수입니다.

튜닝 가이드:

- **Slab을 키우면**: 확보 횟수가 줄어들지만, 낮은 등급(8B×64=512B)에서는 남는 공간이 커집니다.
- **max_alloc_size를 키우면**: 더 큰 메시지도 풀을 타지만, 등급 밖 큰 블록은 파편화에 불리합니다.

---

## 요약

| 항목 | 설명 |
|------|------|
| **알고리즘 계열** | TCMalloc 스타일 스레드 캐싱 할당자 |
| **계층** | ThreadLocalCache(락프리) → CentralCache(뮤텍스) → Slab(4KB) |
| **크기 등급** | 9개 (8B~2048B), 배치 크기 2~64 |
| **빈 블록 관리** | 침입형(intrusive) FreeList — 오버헤드 0 |
| **빠른 경로** | 스레드 로컬 FreeList pop/push — 잠금·시스템콜 없음 |
| **느린 경로** | 배치 리필/반납(드묾) → Slab 신규 확보(더 드묾) |
| **큰 할당** | 2048B 초과·특수 정렬은 `::operator new` 위임 |
| **소유자** | `defaultMemoryPool()` 전역 싱글턴, Message Pool 모드가 사용 |
| **정책** | Poison 디버그 / 예외·abort 실패 처리를 템플릿으로 교체 |
