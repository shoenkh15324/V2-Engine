# 3단계: 성능 측정 및 데이터 수집

> **기간**: 6-10주차 (2-3주, 2단계와 겹침)
> **목표**: 체계적인 성능 측정, 데이터 수집, 시각화
> **전제 조건**: 2단계 (벤치마크 구현 완료)

---

## 목차

1. [목표](#1-목표)
2. [측정 방법론](#2-측정-방법론)
3. [벤치마크 스크립트](#3-벤치마크-스크립트)
4. [데이터 수집](#4-데이터-수집)
5. [시각화](#5-시각화)
6. [환경 문서화](#6-환경-문서화)
7. [재현성](#7-재현성)
8. [구현 단계](#8-구현-단계)
9. [검증 체크리스트](#9-검증-체크리스트)

---

## 1. 목표

### 1.1 주요 목표

1. 체계적 측정을 위한 자동화된 벤치마크 스크립트 작성
2. 여러 설정에서 벤치마크 데이터 수집
3. 시각화 차트 생성 (처리량, 지연 시간, 스케일링)
4. 재현성을 위한 하드웨어/소프트웨어 환경 문서화
5. 비교를 위한 기준선 측정 구축

### 1.2 측정 차원

| 차원 | 범위 | 목적 |
|-----------|-------|---------|
| **워커** | 1, 2, 4, 8, 16, 32, 64 | 워커 스케일링 |
| **액터** | 1, 2, 4, 8, 16, 32, 64 | 액터 스케일링 |
| **MaxBatch** | 8, 16, 32, 64, 128, 256 | 배치 크기 영향 |
| **메일박스 타입** | 뮤텍스, MPSC | 잠금 해제 vs 뮤텍스 비교 |
| **메일박스 크기** | 64, 256, 1024, 4096, 16384 | 용량 영향 |
| **반복** | 10000, 100000, 1000000 | 통계적 유의성 |

---

## 2. 측정 방법론

### 2.1 모범 사례

Google Benchmark 및 학술 벤치마크 기준을 따릅니다:

1. **워밍업**: 측정 전 벤치마크를 한 번 실행하여 캐시와 분기 예측기 워밍업
2. **여러 번 실행**: 각 설정을 5회 이상 실행, 중앙값 및 표준편차 보고
3. **격리**: 측정 중 다른 무거운 프로세스 없음
4. **일관된 환경**: 동일한 CPU 거버너, 동일한 프로세스 우선순위
5. **통계 보고**: 평균, 표준편차, 신뢰 구간

### 2.2 워밍업 전략

```bash
# 각 설정에 대한 스크립트 구조
for config in configurations; do
    # 워밍업 실행 (기록하지 않음)
    run_benchmark $config > /dev/null 2>&1

    # 측정 실행 (5회)
    for i in 1 2 3 4 5; do
        run_benchmark $config --json >> results.json
    done
done
```

### 2.3 통계 분석

각 설정에 대해 다음을 계산:

- **평균**: 실행 간 평균
- **표준편차**: 실행 간 변동
- **변동 계수**: stddev / mean (5% 미만이어야 함)
- **이상치 감지**: 중앙값에서 20% 이상 편차가 있는 실행 제거

### 2.4 피해야 할 측정 오류

| 오류 | 완화 방안 |
|-------|------------|
| 콜드 캐시 효과 | 측정 전 워밍업 실행 |
| CPU 주파수 스케일링 | `performance` 거버너 설정 |
| 열 절하 | 온도 모니터링, 냉각 시간 허용 |
| 백그라운드 프로세스 | 단일 사용자 모드 또는 격리된 컨테이너에서 실행 |
| 측정 오버헤드 | 벤치마크 중 메트릭 비활성화 |
| JIT 컴파일 | 해당 없음 (컴파일된 C++) |
| GC 일시 정지 | 해당 없음 (수동 메모리 관리) |

---

## 3. 벤치마크 스크립트

### 3.1 메인 벤치마크 러너

**파일**: `scripts/benchmark_all.sh`

```bash
#!/bin/bash
# V2-Engine 포괄적 벤치마크 러너
# 사용법: ./scripts/benchmark_all.sh [--output-dir DIR] [--runs N]

set -e

# 설정
OUTPUT_DIR="${1:-benchmark_results}"
NUM_RUNS="${2:-5}"
BUILD_DIR="build"
BINARY="$BUILD_DIR/v2"

# 출력 디렉토리 생성
mkdir -p "$OUTPUT_DIR/raw"
mkdir -p "$OUTPUT_DIR/charts"

echo "=== V2-Engine 벤치마크 스위트 ==="
echo "출력: $OUTPUT_DIR"
echo "설정당 실행 횟수: $NUM_RUNS"
echo ""

# 1. 처리량 벤치마크
echo "[1/7] 처리량 벤치마크 실행 중..."
./scripts/bench_throughput.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 2. 지연 시간 벤치마크
echo "[2/7] 지연 시간 벤치마크 실행 중..."
./scripts/bench_latency.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 3. 경쟁 벤치마크
echo "[3/7] 경쟁 벤치마크 실행 중..."
./scripts/bench_contention.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 4. 스케일링 벤치마크
echo "[4/7] 스케일링 벤치마크 실행 중..."
./scripts/bench_scaling.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 5. 백프레셔 벤치마크
echo "[5/7] 백프레셔 벤치마크 실행 중..."
./scripts/bench_backpressure.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 6. 스케줄러 벤치마크
echo "[6/7] 스케줄러 벤치마크 실행 중..."
./scripts/bench_scheduler.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 7. 메일박스 비교
echo "[7/7] 메일박스 비교 실행 중..."
./scripts/bench_comparison.sh "$OUTPUT_DIR" "$NUM_RUNS"

echo ""
echo "=== 벤치마크 스위트 완료 ==="
echo "원시 데이터: $OUTPUT_DIR/raw/"
echo "차트: $OUTPUT_DIR/charts/"
echo ""
echo "차트 생성: python3 scripts/visualize.py $OUTPUT_DIR"
```

### 3.2 처리량 벤치마크 스크립트

**파일**: `scripts/bench_throughput.sh`

```bash
#!/bin/bash
# 설정별 처리량 벤치마크

OUTPUT_DIR="$1"
NUM_RUNS="$2"
BINARY="build/v2"

WORKERS=(1 2 4 8 16 32 64)
ACTORS=(1 2 4 8 16 32 64)
ITERATIONS=100000
MAXBATCH=32

RESULTS_FILE="$OUTPUT_DIR/raw/throughput.json"

echo "[]" > "$RESULTS_FILE"

for workers in "${WORKERS[@]}"; do
    for actors in "${ACTORS[@]}"; do
        echo "  설정: 워커=$workers, 액터=$actors"

        for run in $(seq 1 $NUM_RUNS); do
            # 워밍업 (첫 번째 실행)
            if [ $run -eq 1 ]; then
                $BINARY benchmark throughput \
                    --workers $workers --actors $actors \
                    --iterations $ITERATIONS --maxbatch $MAXBATCH \
                    > /dev/null 2>&1 || true
            fi

            # 측정
            RESULT=$($BINARY benchmark throughput \
                --workers $workers --actors $actors \
                --iterations $ITERATIONS --maxbatch $MAXBATCH \
                --json 2>/dev/null || echo "{}")

            # 결과 파일에 추가
            echo "$RESULT" >> "$OUTPUT_DIR/raw/throughput_raw.jsonl"
        done
    done
done

echo "  결과가 $RESULTS_FILE에 저장됨"
```

### 3.3 메일박스 비교 스크립트

**파일**: `scripts/bench_comparison.sh`

```bash
#!/bin/bash
# 메일박스 비교 벤치마크

OUTPUT_DIR="$1"
NUM_RUNS="$2"
BINARY="build/v2"

WORKERS=(4 16 64)
ACTORS=(1 16 64)
ITERATIONS=100000
MAXBATCH=32

RESULTS_FILE="$OUTPUT_DIR/raw/comparison.json"

echo "[]" > "$RESULTS_FILE"

for mailbox in mutex mpsc; do
    echo "  메일박스: $mailbox"
    for workers in "${WORKERS[@]}"; do
        for actors in "${ACTORS[@]}"; do
            echo "    설정: 워커=$workers, 액터=$actors"

            for run in $(seq 1 $NUM_RUNS); do
                # 워밍업
                if [ $run -eq 1 ]; then
                    $BINARY benchmark throughput \
                        --workers $workers --actors $actors \
                        --iterations $ITERATIONS --maxbatch $MAXBATCH \
                        --mailbox $mailbox \
                        > /dev/null 2>&1 || true
                fi

                # 측정
                RESULT=$($BINARY benchmark throughput \
                    --workers $workers --actors $actors \
                    --iterations $ITERATIONS --maxbatch $MAXBATCH \
                    --mailbox $mailbox \
                    --json 2>/dev/null || echo "{}")

                # 메타데이터와 함께 저장
                echo "$RESULT" | jq --arg mb "$mailbox" \
                    --argjson w "$workers" --argjson a "$actors" \
                    '. + {mailboxType: $mb, workers: $w, actors: $a}' \
                    >> "$OUTPUT_DIR/raw/comparison_raw.jsonl"
            done
        done
    done
done

echo "  결과가 $RESULTS_FILE에 저장됨"
```

### 3.4 환경 캡처 스크립트

**파일**: `scripts/capture_environment.sh`

```bash
#!/bin/bash
# 벤치마크 환경 정보 캡처

OUTPUT_DIR="$1"
ENV_FILE="$OUTPUT_DIR/environment.json"

echo "환경 캡처 중..."

# 시스템 정보
KERNEL=$(uname -r)
ARCH=$(uname -m)
CPU_MODEL=$(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
CPU_CORES=$(nproc)
MEMORY=$(free -h | awk '/^Mem:/{print $2}')

# 컴파일러 정보
COMPILER=$(g++ --version | head -1)
CMAKE_VERSION=$(cmake --version | head -1)

# 빌드 설정
BUILD_TYPE=$(grep CMAKE_BUILD_TYPE build/CMakeCache.txt 2>/dev/null | cut -d= -f2 || echo "알 수 없음")
CXX_FLAGS=$(grep CMAKE_CXX_FLAGS build/CMakeCache.txt 2>/dev/null | cut -d= -f2 || echo "알 수 없음")

# CPU 거버너
CPU_GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "알 수 없음")

# 온도 (가능한 경우)
TEMPERATURE=$(sensors 2>/dev/null | grep "Core 0" | awk '{print $3}' || echo "N/A")

cat > "$ENV_FILE" << EOF
{
  "timestamp": "$(date -Iseconds)",
  "system": {
    "kernel": "$KERNEL",
    "arch": "$ARCH",
    "cpuModel": "$CPU_MODEL",
    "cpuCores": $CPU_CORES,
    "memory": "$MEMORY",
    "cpuGovernor": "$CPU_GOVERNOR",
    "temperature": "$TEMPERATURE"
  },
  "compiler": {
    "compiler": "$COMPILER",
    "cmake": "$CMAKE_VERSION",
    "buildType": "$BUILD_TYPE",
    "cxxFlags": "$CXX_FLAGS"
  }
}
EOF

echo "환경이 $ENV_FILE에 저장됨"
```

---

## 4. 데이터 수집

### 4.1 데이터 형식

모든 벤치마크 결과는 JSON Lines (`.jsonl`) 형식으로 저장:

```jsonl
{"benchmark":"throughput","config":{"workers":16,"actors":16,"maxBatch":32},"results":{"throughputPerSec":12500.00,"avgLatencyNs":80000.00},"run":1,"timestamp":"2024-01-15T10:30:00Z"}
{"benchmark":"throughput","config":{"workers":16,"actors":16,"maxBatch":32},"results":{"throughputPerSec":12800.00,"avgLatencyNs":78125.00},"run":2,"timestamp":"2024-01-15T10:30:05Z"}
```

### 4.2 원시 데이터 구조

```
benchmark_results/
├── raw/
│   ├── throughput_raw.jsonl
│   ├── latency_raw.jsonl
│   ├── contention_raw.jsonl
│   ├── scaling_raw.jsonl
│   ├── backpressure_raw.jsonl
│   ├── scheduler_raw.jsonl
│   └── comparison_raw.jsonl
├── processed/
│   ├── throughput_summary.json
│   ├── latency_summary.json
│   ├── contention_summary.json
│   ├── scaling_summary.json
│   ├── backpressure_summary.json
│   ├── scheduler_summary.json
│   └── comparison_summary.json
├── charts/
│   ├── throughput_heatmap.png
│   ├── scaling_worker.png
│   ├── scaling_actor.png
│   ├── latency_distribution.png
│   ├── contention_curve.png
│   ├── comparison_bar.png
│   └── comparison_scatter.png
└── environment.json
```

### 4.3 데이터 처리 파이프라인

```bash
# 1. 벤치마크 실행
./scripts/benchmark_all.sh benchmark_results 5

# 2. 원시 데이터 처리
python3 scripts/process_data.py benchmark_results/raw --output benchmark_results/processed

# 3. 차트 생성
python3 scripts/visualize.py benchmark_results/processed --output benchmark_results/charts

# 4. 요약 보고서 생성
python3 scripts/generate_report.py benchmark_results --output benchmark_results/REPORT.md
```

---

## 5. 시각화

### 5.1 차트 유형

| 차트 | 데이터 | 목적 |
|-------|------|---------|
| **처리량 히트맵** | 워커 × 액터 | 최적점 표시 |
| **스케일링 곡선** | 워커/액터 vs 처리량 | 아멀달의 법칙 시연 |
| **지연 시간 분포** | 지연 시간 히스토그램 | 꼬리 지연 시간 분석 |
| **경쟁 곡선** | 프로듀서 vs 처리량 | 스케일링 한계 |
| **비교 바 차트** | 뮤텍스 vs MPSC | 직접 비교 |
| **비교 산점도** | 처리량 vs 지연 시간 | 트레이드오프 시각화 |

### 5.2 시각화 스크립트

**파일**: `scripts/visualize.py`

```python
#!/usr/bin/env python3
"""
V2-Engine 벤치마크 시각화
사용법: python3 scripts/visualize.py benchmark_results/processed --output benchmark_results/charts
"""

import json
import argparse
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import numpy as np
from pathlib import Path

def load_summary(data_dir: Path, benchmark: str) -> dict:
    with open(data_dir / f"{benchmark}_summary.json") as f:
        return json.load(f)

def plot_throughput_heatmap(data: dict, output_dir: Path):
    """워커 × 액터 처리량 히트맵."""
    workers = sorted(set(d["workers"] for d in data["results"]))
    actors = sorted(set(d["actors"] for d in data["results"]))

    matrix = np.zeros((len(workers), len(actors)))
    for d in data["results"]:
        w_idx = workers.index(d["workers"])
        a_idx = actors.index(d["actors"])
        matrix[w_idx, a_idx] = d["throughputPerSec"]

    fig, ax = plt.subplots(figsize=(10, 8))
    im = ax.imshow(matrix, cmap="YlOrRd", aspect="auto")

    ax.set_xticks(range(len(actors)))
    ax.set_xticklabels(actors)
    ax.set_yticks(range(len(workers)))
    ax.set_yticklabels(workers)
    ax.set_xlabel("액터")
    ax.set_ylabel("워커")
    ax.set_title("처리량 히트맵 (메시지/초)")

    plt.colorbar(im, ax=ax)
    plt.tight_layout()
    plt.savefig(output_dir / "throughput_heatmap.png", dpi=150)
    plt.close()

def plot_scaling_curve(data: dict, output_dir: Path, scale_type: str):
    """스케일링 효율성 곡선."""
    x = [d["count"] for d in data["results"]]
    y = [d["throughputPerSec"] for d in data["results"]]
    efficiency = [d["efficiency"] for d in data["results"]]

    fig, ax1 = plt.subplots(figsize=(10, 6))

    color1 = "#2196F3"
    ax1.plot(x, y, "o-", color=color1, linewidth=2, markersize=8, label="처리량")
    ax1.set_xlabel(f"{scale_type.capitalize()} 수")
    ax1.set_ylabel("처리량 (메시지/초)", color=color1)
    ax1.tick_params(axis="y", labelcolor=color1)
    ax1.set_xscale("log", base=2)
    ax1.set_yscale("log", base=10)

    ax2 = ax1.twinx()
    color2 = "#FF5722"
    ax2.plot(x, efficiency, "s--", color=color2, linewidth=2, markersize=8, label="효율성")
    ax2.set_ylabel("효율성 (정규화)", color=color2)
    ax2.tick_params(axis="y", labelcolor=color2)
    ax2.set_ylim(0, 1.1)

    ax1.set_title(f"{scale_type.capitalize()} 스케일링 곡선")
    ax1.legend(loc="upper left")
    ax2.legend(loc="upper right")

    plt.tight_layout()
    plt.savefig(output_dir / f"scaling_{scale_type}.png", dpi=150)
    plt.close()

def plot_comparison_bar(data: dict, output_dir: Path):
    """메일박스 변형 비교 바 차트."""
    variants = data["variants"]
    x = np.arange(len(variants))
    width = 0.35

    fig, ax = plt.subplots(figsize=(10, 6))

    throughput = [v["throughputPerSec"] for v in variants]
    latency = [v["avgLatencyNs"] / 1000 for v in variants]  # 마이크로초로 변환

    bars1 = ax.bar(x - width/2, throughput, width, label="처리량 (메시지/초)", color="#2196F3")
    ax.set_ylabel("처리량 (메시지/초)", color="#2196F3")
    ax.tick_params(axis="y", labelcolor="#2196F3")

    ax2 = ax.twinx()
    bars2 = ax2.bar(x + width/2, latency, width, label="지연 시간 (us)", color="#FF5722")
    ax2.set_ylabel("지연 시간 (us)", color="#FF5722")
    ax2.tick_params(axis="y", labelcolor="#FF5722")

    ax.set_xticks(x)
    ax.set_xticklabels([v["type"] for v in variants])
    ax.set_title("메일박스 비교: 뮤텍스 vs MPSC")

    ax.legend(loc="upper left")
    ax2.legend(loc="upper right")

    plt.tight_layout()
    plt.savefig(output_dir / "comparison_bar.png", dpi=150)
    plt.close()

def main():
    parser = argparse.ArgumentParser(description="벤치마크 차트 생성")
    parser.add_argument("data_dir", type=Path, help="처리된 데이터 디렉토리")
    parser.add_argument("--output", type=Path, default=Path("benchmark_results/charts"))
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)

    # 모든 차트 생성
    throughput = load_summary(args.data_dir, "throughput")
    plot_throughput_heatmap(throughput, args.output)

    # ... 다른 차트 생성 ...

    print(f"차트가 {args.output}에 저장됨")

if __name__ == "__main__":
    main()
```

### 5.3 차트 예제

#### 처리량 히트맵

```
        액터
        1     2     4     8     16    32    64
    ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┐
  1 │ 5000│ 8000│15000│28000│48000│65000│72000│
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
  2 │ 5500│ 9000│17000│32000│55000│75000│85000│
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
워 4 │ 6000│10000│19000│36000│62000│85000│95000│
커    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
  8 │ 6500│11000│21000│40000│70000│95000│105k │
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
 16│ 7000│12000│23000│44000│78000│105k │115k │
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
 32│ 7500│13000│25000│48000│85000│112k │120k │
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
  64│ 8000│14000│27000│52000│90000│118k │125k │
    └─────┴─────┴─────┴─────┴─────┴─────┴─────┘

색상: 낮음 (5k) ░░░ 중간 (60k) ▓▓▓ 높음 (125k) ███
```

---

## 6. 환경 문서화

### 6.1 필요한 정보

| 카테고리 | 필드 |
|----------|--------|
| **하드웨어** | CPU 모델, 코어 수, 클럭 속도, L1/L2/L3 캐시 크기, RAM |
| **OS** | 배포판, 커널 버전, 아키텍처 |
| **컴파일러** | GCC/Clang 버전, C++ 표준, 최적화 플래그 |
| **빌드** | CMake 버전, 빌드 타입, 링커, LTO 상태 |
| **런타임** | CPU 거버너, 터보 부스트 상태, 백그라운드 프로세스 |
| **온도** | 유휴 및 부하 온도 |

### 6.2 환경 파일 예제

```json
{
  "timestamp": "2024-01-15T10:30:00+09:00",
  "system": {
    "kernel": "6.5.0-44-generic",
    "arch": "x86_64",
    "cpuModel": "Intel Core i7-13700K",
    "cpuCores": 16,
    "cpuThreads": 24,
    "cpuBaseClock": "3.4 GHz",
    "cpuBoostClock": "5.4 GHz",
    "l1Cache": "800 KB",
    "l2Cache": "2 MB",
    "l3Cache": "30 MB",
    "memory": "32 GB DDR5-5600",
    "cpuGovernor": "performance",
    "turboBoost": true
  },
  "compiler": {
    "compiler": "g++ (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0",
    "cmake": "cmake version 3.22.1",
    "buildType": "Release",
    "cxxFlags": "-O3 -march=native -DNDEBUG",
    "linker": "gold",
    "ltro": true
  },
  "runtime": {
    "cpuGovernor": "performance",
    "backgroundProcesses": ["systemd", "sshd", "dbus"],
    "idleTemperature": "45°C",
    "loadTemperature": "72°C"
  }
}
```

### 6.3 CPU 거버너 설정

```bash
# 일관된 결과를 위해 performance 거버너 설정
sudo cpupower frequency-set -g performance

# 확인
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# 터로 부스트 비활성화 (선택 사항, 더 일관된 결과를 위해)
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
```

---

## 7. 재현성

### 7.1 재현성 체크리스트

- [ ] 환경 파일 캡처됨
- [ ] 빌드 플래그 문서화됨
- [ ] 벤치마크 스크립트가 멱등적
- [ ] 통계적 유의성을 위해 여러 번 실행 (>= 5)
- [ ] 워밍업 실행 포함됨
- [ ] 결과에 타임스탬프 포함
- [ ] 코드 버전 (git 해시) 기록됨

### 7.2 git 해시 기록

```bash
# 벤치마크 스크립트에서
GIT_HASH=$(git rev-parse --short HEAD)
GIT_BRANCH=$(git branch --show-current)

# 결과에 추가
echo "$RESULT" | jq --arg hash "$GIT_HASH" --arg branch "$GIT_BRANCH" \
    '. + {gitHash: $hash, gitBranch: $branch}' \
    >> results.jsonl
```

### 7.3 Docker 재현성 (선택 사항)

최대 재현성을 위해:

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build \
    g++-11 \
    linux-tools-common linux-tools-generic

# performance 거버너 설정
RUN echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# 프로젝트 복사
COPY . /workspace
WORKDIR /workspace

# 빌드
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -march=native" \
    && cmake --build build

# 벤치마크 실행
CMD ["./scripts/benchmark_all.sh", "benchmark_results", "5"]
```

### 7.4 가상 머신 격리

일관된 결과를 위해 다음을 갖춘 VM에서 실행:
- 고정된 CPU 할당 (오버커밋 없음)
- 전용 코어 (CPU 고정)
- 동일한 호스트에 다른 VM 없음
- 일관된 I/O (공유 스토리지 없음)

---

## 8. 구현 단계

### 6-7주차: 스크립트

| 날 | 작업 | 산출물 |
|-----|------|-------------|
| 1-2 | `benchmark_all.sh` 메인 러너 생성 | 메인 스크립트 |
| 3-4 | `bench_throughput.sh` 생성 | 처리량 스크립트 |
| 5-6 | `bench_latency.sh` 생성 | 지연 시간 스크립트 |
| 7-8 | `bench_contention.sh` 생성 | 경쟁 스크립트 |
| 9 | `bench_scaling.sh` 생성 | 스케일링 스크립트 |
| 10 | `bench_comparison.sh` 생성 | 비교 스크립트 |
| 11 | `capture_environment.sh` 생성 | 환경 스크립트 |

### 7-8주차: 시각화

| 날 | 작업 | 산출물 |
|-----|------|-------------|
| 12-13 | `visualize.py` 생성 | 시각화 스크립트 |
| 14-15 | 히트맵 생성 구현 | 히트맵 차트 |
| 16-17 | 스케일링 곡선 구현 | 스케일링 차트 |
| 18-19 | 비교 차트 구현 | 비교 차트 |
| 20 | 시각화 파이프라인 테스트 | 작동하는 파이프라인 |

### 8-9주차: 데이터 수집

| 날 | 작업 | 산출물 |
|-----|------|-------------|
| 21-22 | 전체 벤치마크 스위트 실행 | 원시 데이터 |
| 23-24 | 데이터 처리 및 요약 | 처리된 데이터 |
| 25-26 | 모든 차트 생성 | 차트 |
| 27-28 | 결과 검증 | 검증된 데이터 |

### 9-10주차: 문서화

| 날 | 작업 | 산출물 |
|-----|------|-------------|
| 29-30 | 환경 문서화 | 환경 문서 |
| 31-32 | 방법론 문서화 | 방법론 문서 |
| 33-34 | 재현성 가이드 작성 | 재현성 문서 |
| 35 | 최종 검증 | 완전한 데이터 패키지 |

---

## 9. 검증 체크리스트

### 9.1 스크립트

- [ ] `benchmark_all.sh`가 7개 벤치마크를 모두 실행
- [ ] 각 벤치마크 스크립트가 `--output-dir`과 `--runs`를 받음
- [ ] 스크립트가 오류를 우아하게 처리
- [ ] 스크립트가 JSON Lines 출력 생성
- [ ] 환경 캡처가 대상 시스템에서 작동

### 9.2 데이터 품질

- [ ] 설정당 최소 5회 실행
- [ ] 변동 계수 < 5%
- [ ] 이상치 실행 없음 (필터링 또는 기록)
- [ ] 타임스탬프가 일관됨
- [ ] git 해시가 기록됨

### 9.3 시각화

- [ ] 처리량 히트맵 생성됨
- [ ] 스케일링 곡선 생성됨
- [ ] 비교 바 차트 생성됨
- [ ] 모든 차트에 레이블과 제목
- [ ] 차트가 PNG (300 DPI)로 저장됨

### 9.4 재현성

- [ ] 환경 파일이 완전함
- [ ] 빌드 플래그가 문서화됨
- [ ] 스크립트가 멱등적 (안전하게 재실행 가능)
- [ ] 결과가 원시 데이터에서 재생성 가능

### 9.5 문서화

- [ ] 방법론이 문서화됨
- [ ] 환경이 문서화됨
- [ ] 결과가 해석 가능
- [ ] 차트에 캡션이 있음

---

## 부록: 문제 해결

### A.1 일반적인 문제

| 문제 | 원인 | 해결책 |
|-------|-------|----------|
| 결과의 높은 분산 | 백그라운드 프로세스 | 단일 사용자 모드에서 실행 |
| CPU 절하 | 열 제한 | 온도 모니터링, 냉각 시간 추가 |
| 일관되지 않은 결과 | CPU 주파수 스케일링 | performance 거버너 설정 |
| 메모리 부족 | 너무 많은 반복 | 반복 또는 액터 수 줄이기 |
| 벤치마크 멈춤 | 메일박스 교착 상태 | 버그 확인, 동시성 줄이기 |

### A.2 성능 튜닝

```bash
# 일관된 결과를 위해 터로 부스트 비활성화
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# performance 거버너 설정
sudo cpupower frequency-set -g performance

# ASLR 비활성화 (선택 사항, 더 일관된 주소를 위해)
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space

# 프로세스 우선순위 설정
sudo nice -n -20 ./benchmark_all.sh
```

### A.3 메모리 모니터링

```bash
# 벤치마크 중 메모리 모니터링
watch -n 1 free -h

# 캐시 미스 모니터링 (perf 필요)
sudo perf stat -e cache-misses,cache-references ./benchmark_all.sh
```
