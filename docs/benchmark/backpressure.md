# Backpressure Benchmark

## 목적

**"메일박스가 꽉 찼을 때 시스템이 어떻게 대처하는가?"**

메일박스 오버플로우 상황에서 시스템의 **안정성과 복구 능력**을 측정합니다. 백프레셔(backpressure)는 시스템이 과부하를 처리하는 메커니즘입니다.

## 설계 원리

### 백프레셔란?

수요가 공급보다 많을 때 시스템이 대처하는 방식:

```
정상 상태:
  프로듀서 → [메일박스: 32/64] → 워커 → handle()
               ↑ 남은 공간 32개

과부하 상태:
  프로듀서 → [메일박스: 64/64 꽉 참!] → 워커 → handle()
               ↑ 새 메시지 거부됨
```

### 왜 백프레셔가 중요한가?

- **시스템 안정성**: 메일박스가 넘치면 메시지 유실 또는 크래시
- **품질 보증**: 처리 가능한 양만큼만 수용 → 나머지는 안전하게 거부
- **복구 능력**: 과부하가 풀린 후 빠르게 정상 상태로 복귀

### 측정 방법

각 실험은 3개의 테스트케이스로 측정합니다:

| 테스트케이스 | 이름 | 설명 | 핵심 측정 |
|-------------|------|------|-----------|
| 1 | Empty Start | workers 먼저 시작, producer > consumer로 flood | Drop rate |
| 2 | Pre-fill | mailbox 미리 채우고 workers 시작 | Drain time |
| 3 | Nearly Full | 80% 채우고 start + flood 동시 | Drop rate + Drain time |

**공통 측정 흐름:**

```
1단계: Flood (메시지 주입)
   - 총 주입 시도: flood-rate × flood-duration (예: 100,000 × 100ms = 10,000,000개)
   - mailbox 가득 차면 거부 (dropped++), 수용되면 (sent++)
   - flood 루프 동안 mailbox.count()로 현재 용량 확인

2단계: Drain (복구)
   - processed == sent 될 때까지 busy-wait 대기
   - 뮤텍스 경쟁 없이 atomic counter로 정확한 완료 시점 측정

3단계: 분석
   - dropRate = dropped / (sent + dropped) × 100
   - drain time = workers 시작 ~ 모든 메시지 처리 완료
```

**테스트케이스별 상세 절차:**

```
테스트케이스 1 (Empty Start):
   1. actorSystem.start() → workers 시작 (비어있는 mailbox)
   2. flood 루프 → mailbox가 아직 안 찼으면 push(), 아니면 drop
   3. flood 종료 후 processed == sent 대기 (drain 측정)
   → workers가 flood 중에 이미 처리 → drop rate ~94% (workers=1, maxBatch=1)

테스트케이스 2 (Pre-fill):
   1. flood 루프 (workers 없음) → mailbox 가득 채울 때까지 반복
   2. actorSystem.start() → workers 시작
   3. processed == sentBefore 대기 (drain 측정)
   → mailbox가 이미 꽉 차서 새 메시지 100% 거부

테스트케이스 3 (Nearly Full):
   1. 80% 사전 채움 (workers 없음)
   2. actorSystem.start() + flood 루프 동시
   3. flood 종료 후 processed == sent 대기 (drain 측정)
   → workers가 flood 중에 처리하지만, 80% 선점으로 인해 drop rate ~94%
```

## 실험 결과

### 테스트 환경

| 항목 | 사양 |
|------|------|
| CPU | AMD Ryzen 7 9800X3D (8코어) |
| RAM | 8 GB (WSL) |
| OS | Linux (WSL Ubuntu-22.04) |

**벤치마크 기본 파라미터:**

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| flood-rate | 100,000 | 총 주입 시도 메시지 수 |
| flood-duration | 100 | 주입 시간 (ms) |
| workers | varies | 워커 스레드 수 |
| maxBatch | varies | 사이클당 최대 처리 메시지 수 |
| mailbox | varies | 메일박스 용량 |

### 실험 1: 메일박스 크기에 따른 변화

**조건**: workers=1, maxBatch=1

#### 테스트케이스 1: Empty Start (workers 먼저 시작)

| Mailbox | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|---------|-----------|------|---------|------------|------------|
| 64 | 94.30% | 570,009 | 9,429,991 | 364.7 ms | 0.0 ms |
| 128 | 94.36% | 563,847 | 9,436,153 | 337.0 ms | 0.0 ms |
| 256 | 94.67% | 532,954 | 9,467,046 | 345.7 ms | 0.1 ms |
| 512 | 94.38% | 561,599 | 9,438,401 | 345.4 ms | 0.1 ms |
| 1024 | 94.69% | 530,565 | 9,469,435 | 339.9 ms | 0.2 ms |
| 2048 | 93.92% | 607,554 | 9,392,446 | 382.6 ms | 0.6 ms |
| 4096 | 94.38% | 561,915 | 9,438,085 | 396.3 ms | 1.1 ms |
| 8192 | 94.00% | 599,631 | 9,400,369 | 407.6 ms | 2.1 ms |

#### 테스트케이스 2: Pre-fill (mailbox 미리 채우고 시작)

| Mailbox | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|---------|-----------|------|---------|------------|------------|
| 64 | 100.00% | 64 | 9,999,936 | 131.2 ms | 0.0 ms |
| 128 | 100.00% | 128 | 9,999,872 | 132.2 ms | 0.1 ms |
| 256 | 100.00% | 256 | 9,999,744 | 131.8 ms | 0.1 ms |
| 512 | 99.99% | 512 | 9,999,488 | 135.0 ms | 0.2 ms |
| 1024 | 99.99% | 1,024 | 9,998,976 | 134.1 ms | 0.7 ms |
| 2048 | 99.98% | 2,048 | 9,997,952 | 134.5 ms | 0.9 ms |
| 4096 | 99.96% | 4,096 | 9,995,904 | 136.6 ms | 1.4 ms |
| 8192 | 99.92% | 8,192 | 9,991,808 | 133.0 ms | 2.5 ms |

#### 테스트케이스 3: Nearly Full (80% 채우고 start + flood 동시)

| Mailbox | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|---------|-----------|------|---------|------------|------------|
| 64 | 94.19% | 580,772 | 9,419,228 | 373.8 ms | 0.0 ms |
| 128 | 94.34% | 566,254 | 9,433,746 | 372.6 ms | 0.0 ms |
| 256 | 94.05% | 594,800 | 9,405,200 | 368.2 ms | 0.1 ms |
| 512 | 94.03% | 596,897 | 9,403,103 | 377.4 ms | 0.1 ms |
| 1024 | 94.25% | 574,606 | 9,425,394 | 371.0 ms | 0.3 ms |
| 2048 | 94.22% | 577,645 | 9,422,355 | 400.9 ms | 0.8 ms |
| 4096 | 94.74% | 526,456 | 9,473,544 | 440.1 ms | 1.0 ms |
| 8192 | 93.94% | 605,669 | 9,394,331 | 409.6 ms | 2.1 ms |

**분석:**
- **Drop rate는 테스트케이스에 관계없이 ~94%**: workers=1, maxBatch=1에서는 producer가 consumer보다 훨씬 빠름
- **Pre-fill (테스트케이스 2)은 drop rate 100%**: workers 시작 전에 mailbox가 이미 꽉 참
- **Drain time은 mailbox에 비례**: 64→8192에서 0.0→2.1ms
- **테스트케이스 1과 3이 유사**: workers가 flood 중에 이미 처리하므로

### 실험 2: maxBatch에 따른 변화

**조건**: workers=1, mailbox=8192

#### 테스트케이스 1: Empty Start

| maxBatch | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|----------|-----------|------|---------|------------|------------|
| 1 | 93.82% | 618,165 | 9,381,835 | 414.4 ms | 2.1 ms |
| 8 | 42.10% | 5,789,900 | 4,210,100 | 1,150.4 ms | 0.7 ms |
| 32 | 6.21% | 9,379,333 | 620,667 | 1,738.2 ms | 0.8 ms |
| 128 | 8.42% | 9,158,393 | 841,607 | 1,700.6 ms | 0.1 ms |

#### 테스트케이스 2: Pre-fill

| maxBatch | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|----------|-----------|------|---------|------------|------------|
| 1 | 99.92% | 8,192 | 9,991,808 | 141.0 ms | 2.7 ms |
| 8 | 99.92% | 8,192 | 9,991,808 | 136.2 ms | 0.6 ms |
| 32 | 99.92% | 8,192 | 9,991,808 | 184.9 ms | 0.4 ms |
| 128 | 99.92% | 8,192 | 9,991,808 | 139.2 ms | 0.2 ms |

#### 테스트케이스 3: Nearly Full

| maxBatch | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|----------|-----------|------|---------|------------|------------|
| 1 | 93.82% | 618,461 | 9,381,539 | 419.8 ms | 2.3 ms |
| 8 | 43.91% | 5,609,197 | 4,390,803 | 1,128.7 ms | 0.5 ms |
| 32 | 1.10% | 9,889,676 | 110,324 | 1,820.4 ms | 0.0 ms |
| 128 | 4.36% | 9,563,691 | 436,309 | 1,802.0 ms | 0.0 ms |

**분석:**
- **maxBatch가 클수록 Drop Rate 감소**: 사이클당 더 많은 메시지 처리
- **Drain 시간은 maxBatch에 반비례**: maxBatch=1은 2.1-2.7ms, maxBatch=128은 0.0-0.2ms
- **Pre-fill (테스트케이스 2)은 항상 100% drop**: mailbox가 이미 꽉 차서 새 메시지 수용 불가
- **테스트케이스 1과 3이 유사**: flood 중 workers가 처리하므로 drop rate가 낮아짐

### 실험 3: 워커 수에 따른 변화

**조건**: maxBatch=32, mailbox=8192

#### 테스트케이스 1: Empty Start

| Workers | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|---------|-----------|------|---------|------------|------------|
| 1 | 2.07% | 9,793,284 | 206,716 | 1,794.8 ms | 0.0 ms |
| 2 | 2.31% | 9,768,623 | 231,377 | 2,341.9 ms | 0.1 ms |
| 4 | 6.49% | 9,350,599 | 649,401 | 21,944.3 ms | 0.0 ms |
| 8 | 2.81% | 9,719,082 | 280,918 | 40,327.9 ms | 0.0 ms |

#### 테스트케이스 2: Pre-fill

| Workers | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|---------|-----------|------|---------|------------|------------|
| 1 | 99.92% | 8,192 | 9,991,808 | 135.1 ms | 0.4 ms |
| 2 | 99.92% | 8,192 | 9,991,808 | 130.7 ms | 0.4 ms |
| 4 | 99.92% | 8,192 | 9,991,808 | 129.8 ms | 0.3 ms |
| 8 | 99.92% | 8,192 | 9,991,808 | 131.8 ms | 0.0 ms |

#### 테스트케이스 3: Nearly Full

| Workers | Drop Rate | Sent | Dropped | Flood Time | Drain Time |
|---------|-----------|------|---------|------------|------------|
| 1 | 0.07% | 9,993,299 | 6,701 | 1,846.5 ms | 0.0 ms |
| 2 | 3.10% | 9,690,210 | 309,790 | 2,502.6 ms | 0.1 ms |
| 4 | 0.35% | 9,965,177 | 34,823 | 25,980.9 ms | 0.1 ms |
| 8 | 5.34% | 9,465,751 | 534,249 | 34,203.4 ms | 0.0 ms |

**분석:**
- **workers가 많을수록 Flood 시간 증가**: 뮤텍스 경쟁으로 인한 전송 느려짐
- **Drop rate는 workers에 관계없이 낮음** (테스트케이스 1, 3): maxBatch=32가 충분히 빨라서
- **Drain time은 ~0ms**: maxBatch=32, mailbox=8192 환경에서 workers가 빠르게 처리
- **Pre-fill (테스트케이스 2)은 항상 100% drop**: mailbox가 이미 꽉 참

### 실험 결과 종합

| 변수 | 테스트케이스 | Drop Rate | Drain Time |
|------|-------------|-----------|------------|
| Mailbox 크기 | 1, 3 | ~94% (모두 유사) | 0.0→2.1ms (비례) |
| Mailbox 크기 | 2 | 100% | 0.0→2.5ms (비례) |
| maxBatch | 1, 3 | 94%→1% (감소) | 2.3→0.0ms (반비례) |
| maxBatch | 2 | 100% (불변) | 2.7→0.2ms (반비례) |
| workers | 1, 3 | 2-6% (낮음) | ~0ms |
| workers | 2 | 100% (불변) | ~0ms |

- **Mailbox**: 수용 한계 결정 → drop rate에 직접 영향
- **maxBatch**: 사이클당 처리량 결정 → drain 속도에 직접 영향
- **workers**: 뮤텍스 경쟁에 따른 Flood 시간 변화, drop rate/drain에 큰 영향 없음

## 출력 예시

```
=== backpressure ===
Measure mailbox overflow behavior

[Config]
  Workers:    4
  Actors:     1
  MaxBatch:   32
  Mailbox:    64
  Warmup:     0

[Backpressure]
  Drop Rate:   70.31%
  Sent:        296,929
  Dropped:     703,071
  Flood Time:  3272.10 ms
  Drain Time:  0.04 ms
```

## 기술적 세부사항

### 백프레셔 구현 메커니즘

V2-Engine의 백프레셔는 **메일박스 풀 시 silent drop**으로 구현됩니다:

```cpp
// Mailbox::push - 고정 크기 링 버퍼
template <typename U>
bool push(U&& msg){
    std::lock_guard<std::mutex> lock(mutex_);
    if(count_ == capacity_){       // 완전히 찬 경우
        return false;              // 거부 (발신자에게 알림 없음)
    }
    buffer_[tail_] = std::forward<U>(msg);
    tail_ = (tail_ + 1) % capacity_;
    ++count_;
    return true;
}

// ActorContext::enqueue - 메시지 수신 경로
void ActorContext::enqueue(Message msg){
    if(!mailbox_.push(std::move(msg))){    // push 실패 = 메일박스 가득 참
        Metrics::recordEnqueue(actor_->id(), false, 0);  // 드롭 기록
        return;                             // 발신자에게 알림 없이 반환
    }
    // 성공 시 스케줄링
    if(dispatcher_ && !scheduled_.exchange(true)){
        dispatcher_->dispatch(this);        // 워커 wake-up
    }
}
```

**핵심 특성:**
- **Silent drop**: `sendMsg()`는 `void` 반환 → 발신자는 드롭 여부를 알 수 없음
- **조건**: `count_ == capacity_` (완전히 찬 경우만 거부, high-water mark 없음)
- **Metric만 기록**: `Metrics::recordEnqueue()`로 드롭 카운트만 증가

### 드레인 메커니즘

드레인은 **세마포어 기반 cooperative scheduling**으로 동작합니다:

```cpp
// Worker 루프
void Worker::runLoop(){
    while(running_){
        ActorContext* actorCtx = dispatcher_->acquire();   // 세마포어 블록
        if(!actorCtx) break;
        int processed = actorCtx->run(maxBatch_);          // 배치 처리
    }
}

// ActorContext::run - 최대 maxBatch개 메시지 처리
int ActorContext::run(int maxBatch){
    Message msg;
    int processed = 0;
    while((maxBatch < 0) || (processed < maxBatch)){
        if(!mailbox_.pop(msg)) break;        // 메일박스 비어있으면 종료
        actor_->handle(msg);                 // 메시지 처리
        processed++;
    }
    scheduled_ = false;                      // 재스케줄링 허용
    if(!mailbox_.empty()){                   // 남은 메시지 있으면 재-dispatch
        dispatcher_->dispatch(this);
    }
    return processed;
}
```

**드레인 흐름:**
1. `dispatch()` → `readyQueue_`에 액터 추가 + `sema_.release()`로 워커 wake-up
2. `Worker::acquire()` → 세마포어에서 워커 깨어남
3. `actorCtx->run(maxBatch)` → 최대 `maxBatch`개 메시지 처리
4. `scheduled_ = false` → 재스케줄링 가능
5. `mailbox_.empty()`가 아니면 `dispatch()` 재호출 → 다음 배치 처리

**maxBatch에 따른 드레인 특성:**
- `maxBatch=-1` (무제한): 사이클 1회로 전체 드레인, 오버헤드 최소
- `maxBatch=32` (기본): 여러 사이클로 나눠서 처리, 재-dispatch 횟수 증가
- `maxBatch=1` (단일): 사이클당 1개, 재-dispatch 오버헤드가 큼

**Flood 중 드레인:**
테스트케이스 1과 3에서는 workers가 flood와 동시에 메시지를 처리합니다. 이것이 drop rate가 낮아지는 이유입니다. 테스트케이스 2 (Pre-fill)은 flood 중에 workers가 없으므로, 시작 후 순수 drain 시간을 측정할 수 있습니다.

**Drain 속도에 영향을 주는 요소:**
- **maxBatch**: 사이클당 처리량 결정 → 핵심 병목
- **뮤텍스**: `pop()` 호출 시 뮤텍스 획득 필요 → workers 수에 관계없이 throughput 한계
- **workers 수**: maxBatch=1에서는 뮤텍스 경쟁으로 인해 drain 속도 개선 없음

### 시스템 안정성 보장

- 백프레셔가 작동하면 **시스템이 크래시하지 않음**
- 거부된 메시지는 발신자에게 알림 가능 (현재 미구현)
- 드레인 후 정상 처리 재개

## 결론

### 핵심 발견

1. **백프레셔가 올바르게 동작하여 시스템 안정성 보장**
   - 메일박스가 가득 차면 메시지를 안전하게 거부
   - 드레인 후 정상 처리 재개

2. **Drop Rate가 백프레셔 성능의 핵심 지표**
   - Drop rate = 거부된 메시지 비율 (낮을수록 좋음)
   - 시스템이 처리할 수 있는 양을 초과하는 메시지가 발생하면 drop 발생

3. **Drain 시간은 mailbox에 남은 메시지 수와 drain 속도에 비례**
   - maxBatch=1, workers=1: ~2.1ms (drain 느림)
   - maxBatch=32, workers=1: ~0.0ms (drain 빠름)
   - workers 수는 drain에 영향 없음 (뮤텍스 bottleneck)

4. **maxBatch가 drop rate와 drain 속도 모두에 영향**
   - maxBatch=1: 93.82% drop (사이클당 1개만 처리)
   - maxBatch=32: 6.21% drop (사이클당 32개 처리)
   - maxBatch=128: 8.42% drop (더 큰 배치는 뮤프스 경쟁으로 오히려 감소)

5. **Mailbox 크기는 drain 시간에 비례**
   - 64: 0.0ms, 8192: 2.1ms
   - 큰 mailbox는 더 많은 메시지를 수용하지만 drain 시간 증가

6. **3개의 테스트케이스로 다양한 측정 가능**
   - Empty Start: flood 중 처리 성능 측정
   - Pre-fill: 순수 drain 시간 측정
   - Nearly Full: 혼합 시나리오 측정

### 권장 사항

1. **maxBatch 설정**: 
   - 낮은 drop rate가 필요하면 maxBatch를 크게 설정 (32 이상)
   - 빠른 drain이 필요하면 maxBatch를 작게 설정 (1-8)

2. **Mailbox 크기 선택**:
   - 빠른 drain이 필요하면 mailbox를 작게 설정 (64-256)
   - 더 많은 메시지를 수용하려면 mailbox를 크게 설정 (1024 이상)

3. **workers 수 조정**:
   - maxBatch가 충분히 크면 workers 수는 drop rate에 큰 영향 없음
   - 뮤프스 경쟁을 줄이려면 workers 수를 적절히 조절
