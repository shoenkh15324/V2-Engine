# Phase 3: Performance Measurement & Data Collection

> **Duration**: Week 6-10 (2-3 weeks, overlaps with Phase 2)
> **Goal**: Systematic performance measurement, data collection, and visualization
> **Prerequisite**: Phase 2 (Benchmarks implemented)

---

## Table of Contents

1. [Objectives](#1-objectives)
2. [Measurement Methodology](#2-measurement-methodology)
3. [Benchmark Scripts](#3-benchmark-scripts)
4. [Data Collection](#4-data-collection)
5. [Visualization](#5-visualization)
6. [Environment Documentation](#6-environment-documentation)
7. [Reproducibility](#7-reproducibility)
8. [Implementation Steps](#8-implementation-steps)
9. [Verification Checklist](#9-verification-checklist)

---

## 1. Objectives

### 1.1 Primary Objectives

1. Create automated benchmark scripts for systematic measurement
2. Collect benchmark data across multiple configurations
3. Generate visualization charts (throughput, latency, scaling)
4. Document hardware/software environment for reproducibility
5. Establish baseline measurements for comparison

### 1.2 Measurement Dimensions

| Dimension | Range | Purpose |
|-----------|-------|---------|
| **Workers** | 1, 2, 4, 8, 16, 32, 64 | Worker scaling |
| **Actors** | 1, 2, 4, 8, 16, 32, 64 | Actor scaling |
| **MaxBatch** | 8, 16, 32, 64, 128, 256 | Batch size impact |
| **Mailbox Type** | Mutex, MPSC | Lock-free vs mutex comparison |
| **Mailbox Size** | 64, 256, 1024, 4096, 16384 | Capacity impact |
| **Iterations** | 10000, 100000, 1000000 | Statistical significance |

---

## 2. Measurement Methodology

### 2.1 Best Practices

Following Google Benchmark and academic benchmarking standards:

1. **Warmup**: Run benchmark once before measuring to warm caches and branch predictors
2. **Multiple runs**: Each configuration runs 5+ times, report median and stddev
3. **Isolation**: No other heavy processes during measurement
4. **Consistent environment**: Same CPU governor, same process priority
5. **Statistical reporting**: Mean, stddev, confidence intervals

### 2.2 Warmup Strategy

```bash
# Script structure for each configuration
for config in configurations; do
    # Warmup run (not recorded)
    run_benchmark $config > /dev/null 2>&1

    # Measurement runs (5 times)
    for i in 1 2 3 4 5; do
        run_benchmark $config --json >> results.json
    done
done
```

### 2.3 Statistical Analysis

For each configuration, calculate:

- **Mean**: Average across runs
- **Standard Deviation**: Variation between runs
- **Coefficient of Variation**: stddev / mean (should be < 5%)
- **Outlier Detection**: Remove runs with > 20% deviation from median

### 2.4 Measurement Errors to Avoid

| Error | Mitigation |
|-------|------------|
| Cold cache effects | Warmup run before measurement |
| CPU frequency scaling | Set `performance` governor |
| Thermal throttling | Monitor temperature, allow cooldown |
| Background processes | Run in single-user mode or isolated container |
| Measurement overhead | Disable metrics during benchmark |
| JIT compilation | Not applicable (compiled C++) |
| GC pauses | Not applicable (manual memory management) |

---

## 3. Benchmark Scripts

### 3.1 Main Benchmark Runner

**File**: `scripts/benchmark_all.sh`

```bash
#!/bin/bash
# V2-Engine Comprehensive Benchmark Runner
# Usage: ./scripts/benchmark_all.sh [--output-dir DIR] [--runs N]

set -e

# Configuration
OUTPUT_DIR="${1:-benchmark_results}"
NUM_RUNS="${2:-5}"
BUILD_DIR="build"
BINARY="$BUILD_DIR/v2"

# Create output directory
mkdir -p "$OUTPUT_DIR/raw"
mkdir -p "$OUTPUT_DIR/charts"

echo "=== V2-Engine Benchmark Suite ==="
echo "Output: $OUTPUT_DIR"
echo "Runs per configuration: $NUM_RUNS"
echo ""

# 1. Throughput Benchmark
echo "[1/7] Running throughput benchmark..."
./scripts/bench_throughput.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 2. Latency Benchmark
echo "[2/7] Running latency benchmark..."
./scripts/bench_latency.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 3. Contention Benchmark
echo "[3/7] Running contention benchmark..."
./scripts/bench_contention.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 4. Scaling Benchmark
echo "[4/7] Running scaling benchmark..."
./scripts/bench_scaling.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 5. Backpressure Benchmark
echo "[5/7] Running backpressure benchmark..."
./scripts/bench_backpressure.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 6. Scheduler Benchmark
echo "[6/7] Running scheduler benchmark..."
./scripts/bench_scheduler.sh "$OUTPUT_DIR" "$NUM_RUNS"

# 7. Mailbox Comparison
echo "[7/7] Running mailbox comparison..."
./scripts/bench_comparison.sh "$OUTPUT_DIR" "$NUM_RUNS"

echo ""
echo "=== Benchmark Suite Complete ==="
echo "Raw data: $OUTPUT_DIR/raw/"
echo "Charts: $OUTPUT_DIR/charts/"
echo ""
echo "Generate charts: python3 scripts/visualize.py $OUTPUT_DIR"
```

### 3.2 Throughput Benchmark Script

**File**: `scripts/bench_throughput.sh`

```bash
#!/bin/bash
# Throughput benchmark across configurations

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
        echo "  Config: workers=$workers, actors=$actors"

        for run in $(seq 1 $NUM_RUNS); do
            # Warmup (first run)
            if [ $run -eq 1 ]; then
                $BINARY benchmark throughput \
                    --workers $workers --actors $actors \
                    --iterations $ITERATIONS --maxbatch $MAXBATCH \
                    > /dev/null 2>&1 || true
            fi

            # Measurement
            RESULT=$($BINARY benchmark throughput \
                --workers $workers --actors $actors \
                --iterations $ITERATIONS --maxbatch $MAXBATCH \
                --json 2>/dev/null || echo "{}")

            # Append to results file
            echo "$RESULT" >> "$OUTPUT_DIR/raw/throughput_raw.jsonl"
        done
    done
done

echo "  Results saved to $RESULTS_FILE"
```

### 3.3 Mailbox Comparison Script

**File**: `scripts/bench_comparison.sh`

```bash
#!/bin/bash
# Mailbox comparison benchmark

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
    echo "  Mailbox: $mailbox"
    for workers in "${WORKERS[@]}"; do
        for actors in "${ACTORS[@]}"; do
            echo "    Config: workers=$workers, actors=$actors"

            for run in $(seq 1 $NUM_RUNS); do
                # Warmup
                if [ $run -eq 1 ]; then
                    $BINARY benchmark throughput \
                        --workers $workers --actors $actors \
                        --iterations $ITERATIONS --maxbatch $MAXBATCH \
                        --mailbox $mailbox \
                        > /dev/null 2>&1 || true
                fi

                # Measurement
                RESULT=$($BINARY benchmark throughput \
                    --workers $workers --actors $actors \
                    --iterations $ITERATIONS --maxbatch $MAXBATCH \
                    --mailbox $mailbox \
                    --json 2>/dev/null || echo "{}")

                # Save with metadata
                echo "$RESULT" | jq --arg mb "$mailbox" \
                    --argjson w "$workers" --argjson a "$actors" \
                    '. + {mailboxType: $mb, workers: $w, actors: $a}' \
                    >> "$OUTPUT_DIR/raw/comparison_raw.jsonl"
            done
        done
    done
done

echo "  Results saved to $RESULTS_FILE"
```

### 3.4 Environment Capture Script

**File**: `scripts/capture_environment.sh`

```bash
#!/bin/bash
# Capture benchmark environment information

OUTPUT_DIR="$1"
ENV_FILE="$OUTPUT_DIR/environment.json"

echo "Capturing environment..."

# System information
KERNEL=$(uname -r)
ARCH=$(uname -m)
CPU_MODEL=$(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
CPU_CORES=$(nproc)
MEMORY=$(free -h | awk '/^Mem:/{print $2}')

# Compiler information
COMPILER=$(g++ --version | head -1)
CMAKE_VERSION=$(cmake --version | head -1)

# Build configuration
BUILD_TYPE=$(grep CMAKE_BUILD_TYPE build/CMakeCache.txt 2>/dev/null | cut -d= -f2 || echo "Unknown")
CXX_FLAGS=$(grep CMAKE_CXX_FLAGS build/CMakeCache.txt 2>/dev/null | cut -d= -f2 || echo "Unknown")

# CPU governor
CPU_GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "Unknown")

# Temperature (if available)
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

echo "Environment saved to $ENV_FILE"
```

---

## 4. Data Collection

### 4.1 Data Format

All benchmark results stored as JSON Lines (`.jsonl`):

```jsonl
{"benchmark":"throughput","config":{"workers":16,"actors":16,"maxBatch":32},"results":{"throughputPerSec":12500.00,"avgLatencyNs":80000.00},"run":1,"timestamp":"2024-01-15T10:30:00Z"}
{"benchmark":"throughput","config":{"workers":16,"actors":16,"maxBatch":32},"results":{"throughputPerSec":12800.00,"avgLatencyNs":78125.00},"run":2,"timestamp":"2024-01-15T10:30:05Z"}
```

### 4.2 Raw Data Structure

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

### 4.3 Data Processing Pipeline

```bash
# 1. Run benchmarks
./scripts/benchmark_all.sh benchmark_results 5

# 2. Process raw data
python3 scripts/process_data.py benchmark_results/raw --output benchmark_results/processed

# 3. Generate charts
python3 scripts/visualize.py benchmark_results/processed --output benchmark_results/charts

# 4. Generate summary report
python3 scripts/generate_report.py benchmark_results --output benchmark_results/REPORT.md
```

---

## 5. Visualization

### 5.1 Chart Types

| Chart | Data | Purpose |
|-------|------|---------|
| **Throughput Heatmap** | workers × actors | Show sweet spots |
| **Scaling Curve** | workers/actors vs throughput | Amdahl's Law demonstration |
| **Latency Distribution** | Latency histogram | Tail latency analysis |
| **Contention Curve** | Producers vs throughput | Scalability limit |
| **Comparison Bar** | Mutex vs MPSC | Direct comparison |
| **Comparison Scatter** | Throughput vs latency | Trade-off visualization |

### 5.2 Visualization Script

**File**: `scripts/visualize.py`

```python
#!/usr/bin/env python3
"""
V2-Engine Benchmark Visualization
Usage: python3 scripts/visualize.py benchmark_results/processed --output benchmark_results/charts
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
    """Workers × Actors heatmap of throughput."""
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
    ax.set_xlabel("Actors")
    ax.set_ylabel("Workers")
    ax.set_title("Throughput Heatmap (msgs/sec)")

    plt.colorbar(im, ax=ax)
    plt.tight_layout()
    plt.savefig(output_dir / "throughput_heatmap.png", dpi=150)
    plt.close()

def plot_scaling_curve(data: dict, output_dir: Path, scale_type: str):
    """Scaling efficiency curve."""
    x = [d["count"] for d in data["results"]]
    y = [d["throughputPerSec"] for d in data["results"]]
    efficiency = [d["efficiency"] for d in data["results"]]

    fig, ax1 = plt.subplots(figsize=(10, 6))

    color1 = "#2196F3"
    ax1.plot(x, y, "o-", color=color1, linewidth=2, markersize=8, label="Throughput")
    ax1.set_xlabel(f"Number of {scale_type.capitalize()}s")
    ax1.set_ylabel("Throughput (msgs/sec)", color=color1)
    ax1.tick_params(axis="y", labelcolor=color1)
    ax1.set_xscale("log", base=2)
    ax1.set_yscale("log", base=10)

    ax2 = ax1.twinx()
    color2 = "#FF5722"
    ax2.plot(x, efficiency, "s--", color=color2, linewidth=2, markersize=8, label="Efficiency")
    ax2.set_ylabel("Efficiency (normalized)", color=color2)
    ax2.tick_params(axis="y", labelcolor=color2)
    ax2.set_ylim(0, 1.1)

    ax1.set_title(f"{scale_type.capitalize()} Scaling Curve")
    ax1.legend(loc="upper left")
    ax2.legend(loc="upper right")

    plt.tight_layout()
    plt.savefig(output_dir / f"scaling_{scale_type}.png", dpi=150)
    plt.close()

def plot_comparison_bar(data: dict, output_dir: Path):
    """Bar chart comparing mailbox variants."""
    variants = data["variants"]
    x = np.arange(len(variants))
    width = 0.35

    fig, ax = plt.subplots(figsize=(10, 6))

    throughput = [v["throughputPerSec"] for v in variants]
    latency = [v["avgLatencyNs"] / 1000 for v in variants]  # Convert to microseconds

    bars1 = ax.bar(x - width/2, throughput, width, label="Throughput (msgs/sec)", color="#2196F3")
    ax.set_ylabel("Throughput (msgs/sec)", color="#2196F3")
    ax.tick_params(axis="y", labelcolor="#2196F3")

    ax2 = ax.twinx()
    bars2 = ax2.bar(x + width/2, latency, width, label="Latency (us)", color="#FF5722")
    ax2.set_ylabel("Latency (us)", color="#FF5722")
    ax2.tick_params(axis="y", labelcolor="#FF5722")

    ax.set_xticks(x)
    ax.set_xticklabels([v["type"] for v in variants])
    ax.set_title("Mailbox Comparison: Mutex vs MPSC")

    ax.legend(loc="upper left")
    ax2.legend(loc="upper right")

    plt.tight_layout()
    plt.savefig(output_dir / "comparison_bar.png", dpi=150)
    plt.close()

def main():
    parser = argparse.ArgumentParser(description="Generate benchmark charts")
    parser.add_argument("data_dir", type=Path, help="Processed data directory")
    parser.add_argument("--output", type=Path, default=Path("benchmark_results/charts"))
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)

    # Generate all charts
    throughput = load_summary(args.data_dir, "throughput")
    plot_throughput_heatmap(throughput, args.output)

    # ... generate other charts ...

    print(f"Charts saved to {args.output}")

if __name__ == "__main__":
    main()
```

### 5.3 Chart Examples

#### Throughput Heatmap

```
        Actors
        1     2     4     8     16    32    64
    ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┐
  1 │ 5000│ 8000│15000│28000│48000│65000│72000│
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
  2 │ 5500│ 9000│17000│32000│55000│75000│85000│
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
W 4 │ 6000│10000│19000│36000│62000│85000│95000│
o    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
r 8 │ 6500│11000│21000│40000│70000│95000│105k │
k    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
e 16│ 7000│12000│23000│44000│78000│105k │115k │
r    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
s 32│ 7500│13000│25000│48000│85000│112k │120k │
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┤
  64│ 8000│14000│27000│52000│90000│118k │125k │
    └─────┴─────┴─────┴─────┴─────┴─────┴─────┘

Color:  Low (5k) ░░░ Medium (60k) ▓▓▓ High (125k) ███
```

---

## 6. Environment Documentation

### 6.1 Required Information

| Category | Fields |
|----------|--------|
| **Hardware** | CPU model, core count, clock speed, L1/L2/L3 cache sizes, RAM |
| **OS** | Distribution, kernel version, architecture |
| **Compiler** | GCC/Clang version, C++ standard, optimization flags |
| **Build** | CMake version, build type, linker, LTO status |
| **Runtime** | CPU governor, turbo boost status, background processes |
| **Temperature** | Idle and load temperatures |

### 6.2 Example Environment File

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

### 6.3 CPU Governor Settings

```bash
# Set performance governor for consistent results
sudo cpupower frequency-set -g performance

# Verify
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# Disable turbo boost (optional, for more consistent results)
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
```

---

## 7. Reproducibility

### 7.1 Reproducibility Checklist

- [ ] Environment file captured
- [ ] Build flags documented
- [ ] Benchmark script is idempotent
- [ ] Multiple runs (>= 5) for statistical significance
- [ ] Warmup runs included
- [ ] Results include timestamp
- [ ] Code version (git hash) recorded

### 7.2 Git Hash Recording

```bash
# In benchmark script
GIT_HASH=$(git rev-parse --short HEAD)
GIT_BRANCH=$(git branch --show-current)

# Add to results
echo "$RESULT" | jq --arg hash "$GIT_HASH" --arg branch "$GIT_BRANCH" \
    '. + {gitHash: $hash, gitBranch: $branch}' \
    >> results.jsonl
```

### 7.3 Docker Reproducibility (Optional)

For maximum reproducibility:

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build \
    g++-11 \
    linux-tools-common linux-tools-generic

# Set performance governor
RUN echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# Copy project
COPY . /workspace
WORKDIR /workspace

# Build
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -march=native" \
    && cmake --build build

# Run benchmarks
CMD ["./scripts/benchmark_all.sh", "benchmark_results", "5"]
```

### 7.4 Virtual Machine Isolation

For consistent results, run in a VM with:
- Fixed CPU allocation (no overcommit)
- Dedicated core(s) (CPU pinning)
- No other VMs on the same host
- Consistent I/O (no shared storage)

---

## 8. Implementation Steps

### Week 6-7: Scripts

| Day | Task | Deliverable |
|-----|------|-------------|
| 1-2 | Create `benchmark_all.sh` main runner | Main script |
| 3-4 | Create `bench_throughput.sh` | Throughput script |
| 5-6 | Create `bench_latency.sh` | Latency script |
| 7-8 | Create `bench_contention.sh` | Contention script |
| 9 | Create `bench_scaling.sh` | Scaling script |
| 10 | Create `bench_comparison.sh` | Comparison script |
| 11 | Create `capture_environment.sh` | Environment script |

### Week 7-8: Visualization

| Day | Task | Deliverable |
|-----|------|-------------|
| 12-13 | Create `visualize.py` | Visualization script |
| 14-15 | Implement heatmap generation | Heatmap chart |
| 16-17 | Implement scaling curves | Scaling charts |
| 18-19 | Implement comparison charts | Comparison charts |
| 20 | Test visualization pipeline | Working pipeline |

### Week 8-9: Data Collection

| Day | Task | Deliverable |
|-----|------|-------------|
| 21-22 | Run full benchmark suite | Raw data |
| 23-24 | Process and summarize data | Processed data |
| 25-26 | Generate all charts | Charts |
| 27-28 | Validate results | Validated data |

### Week 9-10: Documentation

| Day | Task | Deliverable |
|-----|------|-------------|
| 29-30 | Document environment | Environment docs |
| 31-32 | Document methodology | Methodology docs |
| 33-34 | Create reproducibility guide | Reproducibility docs |
| 35 | Final validation | Complete data package |

---

## 9. Verification Checklist

### 9.1 Scripts

- [ ] `benchmark_all.sh` runs all 7 benchmarks
- [ ] Each benchmark script accepts `--output-dir` and `--runs`
- [ ] Scripts handle errors gracefully
- [ ] Scripts produce JSON Lines output
- [ ] Environment capture works on target system

### 9.2 Data Quality

- [ ] At least 5 runs per configuration
- [ ] Coefficient of variation < 5%
- [ ] No outlier runs (filtered or noted)
- [ ] Timestamps are consistent
- [ ] Git hash is recorded

### 9.3 Visualization

- [ ] Throughput heatmap generated
- [ ] Scaling curves generated
- [ ] Comparison bar chart generated
- [ ] All charts are labeled and titled
- [ ] Charts are saved as PNG (300 DPI)

### 9.4 Reproducibility

- [ ] Environment file is complete
- [ ] Build flags are documented
- [ ] Script is idempotent (can re-run safely)
- [ ] Results can be regenerated from raw data

### 9.5 Documentation

- [ ] Methodology is documented
- [ ] Environment is documented
- [ ] Results are interpretable
- [ ] Charts have captions

---

## Appendix: Troubleshooting

### A.1 Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| High variance in results | Background processes | Run in single-user mode |
| CPU throttling | Thermal limits | Monitor temperature, add cooldown |
| Inconsistent results | CPU frequency scaling | Set performance governor |
| Out of memory | Too many iterations | Reduce iterations or actors |
| Benchmark hangs | Deadlock in mailbox | Check for bugs, reduce concurrency |

### A.2 Performance Tuning

```bash
# Disable turbo boost for consistent results
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# Set performance governor
sudo cpupower frequency-set -g performance

# Disable ASLR (optional, for more consistent addresses)
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space

# Set process priority
sudo nice -n -20 ./benchmark_all.sh
```

### A.3 Memory Monitoring

```bash
# Monitor memory during benchmark
watch -n 1 free -h

# Monitor cache misses (requires perf)
sudo perf stat -e cache-misses,cache-references ./benchmark_all.sh
```
