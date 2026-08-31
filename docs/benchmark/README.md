# V2-Engine 벤치마크 스위트

V2-Engine 액터 시스템의 체계적 성능 측정.

> **측정 기준점**: 아래 수치는 2026-08-31 Release + LTO 빌드 기준.

## 벤치마크 목록

| 벤치마크 | 목적 | 핵심 지표 | 핵심 결과 |
|---------|------|----------|----------|
| [throughput](throughput.md) | 종단간 메시지 처리 속도 | msgs/sec | **27.7M msgs/sec** (1W, 1A) |
| [latency](latency.md) | 단일 메시지 종단간 레이턴시 | P50/P99 | P50=912ns, P99=977ns (1W) |
| [contention](contention.md) | 멀티 프로듀서 동시 push | msgs/sec | **19.7M msgs/sec** (1P, 1W) |
| [scaling](scaling.md) | 워커/액터 스케일링 효율성 | 효율성 곡선 | 16A 16W에서 붕괴 확인 |
| [backpressure](backpressure.md) | 메일박스 오버플로우 동작 | 드롭율 | maxBatch>=32에서 0% 드롭 |
| [scheduler](scheduler.md) | 타이머 스케줄링 정밀도 | 발화 정확도 | **99.99% 정확도** |

## 실행 방법

```bash
# 개별 벤치마크
./build/v2_bench_cli <이름> [options]

# 예시
./build/v2_bench_cli throughput --workers 4 --actors 1 --iterations 500000
./build/v2_bench_cli latency --iterations 100000
./build/v2_bench_cli contention --producers 8
./build/v2_bench_cli scaling --iterations 200000 --scale-max 16
./build/v2_bench_cli backpressure
./build/v2_bench_cli scheduler --interval 50 --duration 5000
./build/v2_bench_cli all       # 전체 실행
```

## 테스트 환경

| 항목 | 사양 |
|------|------|
| CPU | AMD Ryzen 7 9800X3D (8 Physical, 16 Logical, Zen 5) |
| 캐시 | L3 96MB (3D V-Cache), L2 1MB/core |
| RAM | 8 GB (DDR5) |
| OS | Ubuntu 22.04.5 LTS (WSL2, 커널 6.18.33) |
| 컴파일러 | g++-14 (Ubuntu 14.3.0) |
| C++ 표준 | C++20 |
| 빌드 시스템 | CMake 3.22.1 + Ninja |
| 빌드 모드 | Release (`-O3 -DNDEBUG`) + LTO |
| 메모리 할당자 | TCMalloc 기반 Slab 풀 (커스텀) |

## 핵심 성능 지표 (2026-08-31 실측)

| 지표 | 값 | 조건 |
|------|-----|------|
| **최대 쓰루풋 (단일 프로듀서)** | 27.7M msgs/sec | workers=1, actors=1 |
| **최대 쓰루풋 (멀티 프로듀서)** | **73.3M msgs/sec** | workers=4, actors=16, producers=8 |
| **최저 P50 레이턴시** | 912 ns | workers=1 |
| **최저 P99 레이턴시** | 977 ns | workers=1 |
| **최대 동시 push** | 19.7M msgs/sec | producers=1, workers=1 |
| **타이머 정확도** | 99.99% | 100ms 간격, ±5ms 지터 |

## 상용 엔진과의 비교

| 엔진 | 벤치마크 유형 | 쓰루풋 | 테스트 환경 |
|------|-------------|--------|-----------|
| **V2-Engine** | 1W 1A 1P | **27.7M msgs/sec** | Ryzen 9800X3D, g++-14, Release+LTO |
| **V2-Engine** | 4W 16A 8P | **73.3M msgs/sec** | 동일 |
| **CAF** (C++) | simple_streaming (in-process) | ~9.4M msgs/sec | Intel i7, GCC 4.9 (2019) |
| **SObjectizer** (C++) | Ping-Pong (direct channel) | ~17M msgs/sec | Intel i7-11850H (2024) |
| **Actix** (Rust) | 라운드로빈 100A (32B) | ~4.7M msgs/sec | Intel i7 Quad-Core |
| **Ractor** (Rust) | 라운드로빈 100A (32B) | ~3.1M msgs/sec | 동일 |

> V2-Engine은 C++ 범용 액터 프레임워크 대비 **2-5배**, Rust 액터 프레임워크 대비 **6-9배** 빠름.

## 시스템 평가

### 장점

1. **초고속 쓰루풋**: 단일 워커 27.7M, 멀티 프로듀서 73.3M msgs/sec
2. **초저 레이턴시**: P50=912ns, P99=977ns (단일 워커)
3. **높은 동시성**: 멀티 프로듀서에서 19.7M msgs/sec
4. **정확한 타이머**: 99.99% 정확도

### 핵심 병목

1. **워커 스케일링 붕괴**: 16 액터 + 16 워커 + 단일 프로듀서에서 0.3M msgs/sec로 붕괴
2. **CAS 경쟁**: 프로듀서 2개에서 33% 성능 하락 (tail_ CAS)
3. **레이턴시 회귀**: 스핀 티어 도입 후 P50 378ns → 912ns로 상승

### 개선 사항

| 우선순위 | 개선 사항 | 예상 효과 |
|---------|----------|----------|
| 최우선 | MPMC 컨슈머 경쟁 제거 | 4W+ 성능 2-3배 향상 |
| 높음 | dispatch 경로 멀티 스레드화 | 단일 프로듀서+멀티 액터 성능 향상 |
| 중간 | 스핀 티어 튜닝 (grace=0 실험) | P50 레이턴시 300-500ns 절감 |
| 중간 | 인플라이트 슬롯 확장 (1024→4096) | 멀티 액터 시 콜리전 감소 |

## 참고 문헌

- 각 벤치마크는 위 링크된 상세 문서 참조
- [로드맵](../plans/roadmap.md): 성능 개선 과제 및 상용 엔진 비교
- 소스 코드: `bench/`
