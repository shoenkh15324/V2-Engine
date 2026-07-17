# V2-Engine Benchmark Suite

V2-Engine actor 시스템의 성능을 체계적으로 측정하기 위한 6개 벤치마크 모음입니다.

## 벤치마크 목록

| 벤치마크 | 목적 | 핵심 지표 |
|---------|------|----------|
| [throughput](throughput.md) | 메시지 처리량 측정 | msgs/sec |
| [latency](latency.md) | 메시지당 레이턴시 분포 | P50/P95/P99/Max |
| [contention](contention.md) | 다중 프로듀서 뮤프스 경쟁 | 동시 push 성능 |
| [scaling](scaling.md) | 워커/액터 스케일링 효율 | 효율 곡선 |
| [backpressure](backpressure.md) | 메일박스 오버플로우 동작 | drop rate |
| [scheduler](scheduler.md) | 타이머 정밀도 측정 | 간격 드리프트 |

## 실행 방법

```bash
# 전체 벤치마크 실행
./build/test/benchmark_runner

# 개별 벤치마크 (CLI)
v2_cli benchmark throughput --workers 4 --actors 4 --iterations 50000
```

## 테스트 환경

- **CPU**: Linux x86_64
- **Compiler**: GCC, C++20
- **커널**: Linux 6.x (timerfd + epoll)
- **메일박스**: 뮤프스 기반 고정 크기 링 버퍼
- ** scheduling**: semaphore + epoll 이벤트 루프

## 참고 문헌

- each benchmark document 참조
- `src/core/perf/benchmark/` 소스 코드 참조
