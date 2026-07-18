# V2-Engine 벤치마크 스위트

V2-Engine 액터 시스템의 체계적 성능 측정.

## 벤치마크 목록

| 벤치마크 | 목적 | 핵심 지표 | 핵심 결과 |
|---------|------|----------|----------|
| [throughput](throughput.md) | 종단간 메시지 처리 속도 | msgs/sec | **7.1M msgs/sec** (workers=2) |
| [latency](latency.md) | 단일 메시지 종단간 레이턴시 | P50/P99 | P50=378ns, P99=641ns (workers=1) |
| [contention](contention.md) | 멀티 프로듀서 동시 push | msgs/sec | **7.97M msgs/sec** (프로듀서 2개) |
| [scaling](scaling.md) | 워커/액터 스케일링 효율성 | 효율성 곡선 | 단일 워커 최적 |
| [backpressure](backpressure.md) | 메일박스 오버플로우 동작 | 드롭율 | maxBatch>=32에서 0% 드롭 |
| [scheduler](scheduler.md) | 타이머 스케줄링 정밀도 | 발화 정확도 | **100% 정확도** |

## 실행 방법

```bash
# 개별 벤치마크
./build/v2_cli benchmark <이름> [options]

# 예시
./build/v2_cli benchmark throughput --workers 4 --actors 1
./build/v2_cli benchmark latency --iterations 50000
./build/v2_cli benchmark contention --producers 8
./build/v2_cli benchmark scaling
./build/v2_cli benchmark backpressure
./build/v2_cli benchmark scheduler --interval 50 --duration 5000
```

## 테스트 환경

| 항목 | 사양 |
|------|------|
| CPU | AMD Ryzen 7 9800X3D (8코어) |
| RAM | 8 GB (WSL) |
| OS | Linux (WSL Ubuntu-22.04) |
| 컴파일러 | GCC, C++20, CMake + Ninja |
| 커널 | Linux 6.x (timerfd, epoll) |
| 빌드 모드 | Release (LTO 활성화) |
| 도구 | `./build/v2_cli benchmark <이름>` |

## 핵심 성능 지표

| 지표 | 값 | 조건 |
|------|-----|------|
| **최대 쓰루풋** | 7.1M msgs/sec | workers=2, actors=1 |
| **최저 P50** | 378 ns | workers=1 |
| **최저 P99** | 641 ns | workers=1 |
| **최대 동시 push** | 7.97M msgs/sec | 프로듀서 2개 |
| **타이머 정확도** | 100% | 모든 간격 |

## 시스템 평가

### 장점

1. **초저 레이턴시**: workers=1에서 P50=378ns, P99=641ns
2. **높은 쓰루풋**: workers=2에서 7.1M msgs/sec
3. **우수한 경합 처리**: 7.97M msgs/sec 동시 push
4. **안정적인 백프레셔**: maxBatch>=32에서 0% 드롭
5. **정확한 타이머**: 100% 정확도, 0.02% 드리프트

### 제약 사항

1. **MPSC 제약**: 액터당 단일 컨슈머, 멀티 워커 시 성능 저하
2. **메일박스 크기 민감**: 8192 미만에서 쓰루풋 붕괴
3. **테일 레이턴시**: workers=4+에서 P99 ~27μs (시스템 오버헤드)

### 개선 사항

| 우선순위 | 개선 사항 | 예상 효과 |
|---------|----------|----------|
| 높음 | 동적 maxBatch 조정 | 레이턴시/쓰루풋 최적화 |
| 중간 | 핸들러 무게 프로파일링 | 최적 워커 수 결정 |
| 중간 | P99 레이턴시 모니터링 | 실시간 안정성 추적 |
| 낮음 | 하드웨어 타이머 사용 | 지터 감소 |

## 참고 문헌

- 각 벤치마크는 위 링크된 상세 문서 참조
- [아키텍처](../architecture/mailbox_comparison.md): 메일박스 선택 기준
- 소스 코드: `src/core/perf/benchmark/`
