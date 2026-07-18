# V2-Engine Benchmark Suite

V2-Engine actor 시스템의 성능을 체계적으로 측정하기 위한 6개 벤치마크 모음입니다.

## 벤치마크 목록

| 벤치마크 | 목적 | 핵심 지표 | 핵심 결과 |
|---------|------|----------|----------|
| [throughput](throughput.md) | 메시지 처리량 측정 | msgs/sec | ~900K msgs/sec (싱글 워커) |
| [latency](latency.md) | 메시지당 레이턴시 분포 | P50/P95/P99/Max | P50=357ns, P99=881ns |
| [contention](contention.md) | 다중 프로듀서 뮤텍스 경쟁 | 동시 push 성능 | ~900K msgs/sec (32 프로듀서) |
| [scaling](scaling.md) | 워커/액터 스케일링 효율 | 효율 곡선 | 싱글 워커 최적 (~900K 고정) |
| [backpressure](backpressure.md) | 메일박스 오버플로우 동작 | drop rate | 백프레셔 정상 동작 |
| [scheduler](scheduler.md) | 타이머 정밀도 측정 | 발사 정확도 | 100% 정확도 |

## 실행 방법

```bash
# 전체 벤치마크 실행
./build/test/benchmark_runner

# 개별 벤치마크 (CLI)
./build/v2_cli benchmark throughput --workers 4 --actors 4 --iterations 50000
```

## 테스트 환경

| 항목 | 값 |
|------|-----|
| CPU | AMD Ryzen 7 9800X3D (8코어) |
| RAM | 8 GB (WSL) |
| OS | Linux (WSL Ubuntu-22.04) |
| Compiler | GCC, C++20, CMake + Ninja |
| 커널 | Linux 6.x (timerfd, epoll) |
| 빌드 모드 | Release (LTO 활성화) |
| 테스트 도구 | CLI benchmark (`./build/v2_cli benchmark <name>`) |

## 핵심 성능 지표

| 지표 | 값 | 조건 |
|------|-----|------|
| **최대 처리량** | 900K msgs/sec | single worker, 1 actor |
| **최저 P50** | 357 ns | workers=1 |
| **최저 P99** | 881 ns | workers=1 |
| **최대 동시 push** | 907K msgs/sec | 2 producers |
| **타이머 정확도** | 100% | 모든 간격/워커 |
| **타이머 드리프트** | 0.02% | interval=50ms |

## 벤치마크별 심층 분석

### 1. Throughput: 시스템의 처리 능력

**결론**: 싱글 워커에서 초당 ~900K 메시지 처리

**핵심 수치**:
- 싱글 워커, 1 액터, maxBatch=1~8: **866K~902K msgs/sec**
- 4 워커, 1 액터, maxBatch=1: **810K msgs/sec**
- 메일박스 65,536, 4 워커: **421K msgs/sec**

**변수별 영향**:

| 변수 | 영향 | 수치 |
|------|------|------|
| Workers ↑ | **감소** | 1→8: 902K→207K (-76%) |
| Actors ↑ | **감소** | 1→4: 923K→111K (-76%) |
| maxBatch ↑ | **감소** | 1→64: 811K→341K (-58%) |
| Mailbox ↑ | **증가** | <8192: ~3.3K → ≥65536: ~420K |

**메일박스 크기가 최대 병목**: 8192 미만이면 ~3,300 msgs/sec로 폭락 (프로듀서 블로킹).

### 2. Latency: 실시간성 검증

**결론**: P50=357ns, P99=881ns → 극도로 낮은 레이턴시 (workers=1)

**워커 수별 레이턴시 분포** (maxBatch=32):

| Workers | P50 | P95 | P99 | P999 | Max |
|---------|-----|-----|-----|------|-----|
| 1 | **357ns** | 472ns | **881ns** | 15.3μs | 231μs |
| 2 | 390ns | 3.4μs | 12.3μs | 35.0μs | 169μs |
| 4 | 469ns | 13.5μs | 18.5μs | 44.1μs | 247μs |
| 8 | 1.1μs | 14.5μs | 28.4μs | 54.1μs | 335μs |

**핵심 발견**:
- **workers=1이 최저 레이턴시**: 세마포어 웨이크업 + 뮤텍스 비용만 소요
- **P95~P99는 maxBatch와 무관**: 15μs~28μs 고정

### 3. Contention: 동시성 처리 능력

**결론**: 32개 프로듀서에서 ~900K msgs/sec → 뮤텍스 push 경쟁 없음

**핵심 수치**:
- 2 프로듀서: **907K msgs/sec** (최고)
- 8 프로듀서: **894K msgs/sec** (maxBatch=1)
- 32 프로듀서: **~890K msgs/sec** (포화 없음)

**뮤트ックス push 오버헤드**: ~100ns critical section. 32개 스레드가 동시에 push해도 포화되지 않음.

### 4. Scaling: 확장성 한계

**결론**: 경량 핸들러에서는 **싱글 워커가 최적** (~900K 고정)

**워커 스케일링**:

| Fixed Actors | 1 worker | 2 workers | 4 workers | 8 workers |
|-------------|----------|-----------|-----------|-----------|
| actors=1 | 902K (1.00x) | 918K (0.51x) | 580K (0.16x) | 334K (0.05x) |
| actors=4 | 923K (1.00x) | 664K (0.36x) | 105K (0.03x) | 55K (0.01x) |
| actors=8 | 915K (1.00x) | 707K (0.39x) | 102K (0.03x) | 36K (0.00x) |

**핵심**: workers=1에서 1→8 액터까지 ~900K 고정 유지. 액터 스케일링은 **싱글 워커에서만 효과적**.

### 5. Backpressure: 안정성 검증

**결론**: 백프레셔가 올바르게 동작하여 시스템 안정성 보장

**핵심 수치** (workers=1, flood=100K msgs, duration=100ms):

| maxBatch | Drop Rate | Drain Time |
|----------|-----------|------------|
| 1 | 93.8% | 2.1ms |
| 8 | 42.1% | 0.7ms |
| 32 | 6.2% | 0.8ms |
| 128 | 8.4% | 0.1ms |

### 6. Scheduler: 시간 정밀도 검증

**결론**: 100% 발사 정확도, 드리프트 ~0.02%, 지터 ±3ms

**핵심 수치** (interval=50ms, workers=4):

| Metric | Value |
|--------|-------|
| Avg Interval | 49.99ms |
| P50 | 49.89ms |
| P95 | 51.08ms |
| P99 | 51.50ms |
| Drift | ~0.02% |
| Jitter | ~3ms |

## 시스템 전체 평가

### 강점

1. **극도로 낮은 레이턴시**: P50=357ns (workers=1)
2. **높은 처리량**: ~900K msgs/sec (싱글 워커)
3. **안정적인 백프레셔**: 과부하 시 안전하게 throttle
4. **정확한 타이머**: 100% 발사 정확도, 0.02% 드리프트
5. **강력한 뮤텍스**: 32개 프로듀서 동시 push에서도 포화 없음

### 한계

1. **스케일링 부재**: 경량 핸들러에서 워커 증가 시 성능 저하 (동기화 비용 > 핸들링 비용)
2. **배치 처리 트레이드오프**: maxBatch 증가 시 throughput 감소
3. **메일박스 크기 민감**: 8192 미만이면 throughput 폭락
4. **지터**: ±3ms는 실시간 오디오/비디오에는 부족

### 개선 방안

| 우선순위 | 개선 사항 | 기대 효과 |
|---------|----------|----------|
| 높음 | Lock-free 메일박스 도입 | 스케일링 개선 |
| 높음 | maxBatch 동적 조절 | 레이턴시/처리량 최적화 |
| 중간 | 핸들러 무게 프로파일링 | 최적 워커 수 결정 |
| 중간 | P99 레이턴시 모니터링 | 실시간 안정성 추적 |
| 낮음 | 하드웨어 타이머 활용 | 지터 개선 |

## 참고 문헌

- 각 벤치마크별 상세 문서 참조
- `src/core/perf/benchmark/` 소스 코드 참조
