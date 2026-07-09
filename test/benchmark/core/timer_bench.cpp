#include <benchmark/benchmark.h>
#include "core/common/time/timer.hpp"
#include <vector>
#include <cstdint>

// Add latency under controlled heap pressure.
// prefill timers are added+cleared inside PauseTiming per iteration,
// so heap size is exactly prefill when add() is measured.

static void TimerAddWithSchedule(benchmark::State& state){
    const int prefill = static_cast<int>(state.range(0));
    for(auto _ : state){
        state.PauseTiming();
        Timer t;
        t.start();
        for(int i = 0; i < prefill; ++i){
            t.add(1000000, false, [](int){});
        }
        state.ResumeTiming();
        benchmark::DoNotOptimize(t.add(1000000, false, [](int){}));
        state.PauseTiming();
        t.stop();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TimerAddWithSchedule)
    ->Arg(0)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// Cancel latency: mark a single timer as dead in a heap of size prefill.

static void TimerCancelMark(benchmark::State& state){
    int prefill = state.range(0);
    for(auto _ : state){
        Timer t;
        t.start();
        state.PauseTiming();
        int id;
        for(int i = 0; i < prefill; i++){
            id = t.add(1000000, false, [](int){});
        }
        state.ResumeTiming();
        t.cancel(id);
        state.PauseTiming();
        t.stop();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TimerCancelMark)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// Fire ready timers.
// Setup adds N zero-delay timers while paused.
// Measured body is only handleTimerEvent().

static void TimerFireReady(benchmark::State& state){
    const int n = static_cast<int>(state.range(0));
    for(auto _ : state){
        state.PauseTiming();
        Timer t;
        t.start();
        int count = 0;
        for(int i = 0; i < n; ++i){
            t.add(0, false, [&count](int){
                ++count;
            });
        }
        state.ResumeTiming();
        t.handleTimerEvent();
        benchmark::DoNotOptimize(count);
        state.PauseTiming();
        t.stop();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(TimerFireReady)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// Repeating timer not-yet-expired fast path.
// This does NOT really measure reschedule cost unless the timer expires.

static void TimerRepeatingNotYetExpired(benchmark::State& state){
    for(auto _ : state){
        state.PauseTiming();
        Timer t;
        t.start();
        int count = 0;
        t.add(1, true, [&count](int){ ++count; });
        state.ResumeTiming();
        t.handleTimerEvent();
        benchmark::DoNotOptimize(count);
        state.PauseTiming();
        t.stop();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TimerRepeatingNotYetExpired);

// Bulk clear.
// Setup adds N future timers while paused.
// Measured body is clear().

static void TimerClearBulk(benchmark::State& state){
    const int n = static_cast<int>(state.range(0));
    for(auto _ : state){
        state.PauseTiming();
        Timer t;
        t.start();
        for(int i = 0; i < n; ++i){
            t.add(1000000, false, [](int){});
        }
        state.ResumeTiming();
        t.clear();
        state.PauseTiming();
        t.stop();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(TimerClearBulk)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// Batch add.
// Measures adding N timers end-to-end.
// stop()/clear teardown is excluded.

static void TimerAddBatch(benchmark::State& state){
    const int n = static_cast<int>(state.range(0));
    for(auto _ : state){
        state.PauseTiming();
        Timer t;
        t.start();
        state.ResumeTiming();
        for(int i = 0; i < n; ++i){
            benchmark::DoNotOptimize(t.add(1000000, false, [](int){}));
        }
        state.PauseTiming();
        t.stop();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(TimerAddBatch)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// Mixed real-world-ish workload.
// Measures add + cancel + fire together.
// No PauseTiming inside body on purpose.

static void TimerMixedWorkload(benchmark::State& state){
    const int n = static_cast<int>(state.range(0));
    const int addCount = n * 60 / 100;
    const int cancelCount = n * 30 / 100;
    for(auto _ : state){
        state.PauseTiming();
        Timer t;
        t.start();
        state.ResumeTiming();
        std::vector<int> ids;
        ids.reserve(addCount);
        for(int i = 0; i < addCount; ++i){
            const bool repeating = (i % 20 == 0); // 5%
            const uint64_t delay = (i % 5 == 0) ? 0 : 1000000; // 20% expired
            ids.push_back(t.add(delay, repeating, [](int){}));
        }
        for(int i = 0; i < cancelCount && i < static_cast<int>(ids.size()); ++i){
            t.cancel(ids[i]);
        }
        t.handleTimerEvent();
        state.PauseTiming();
        t.stop();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(TimerMixedWorkload)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// Tick fast path.
// Measures handleTimerEvent() when no timer is expired.

static void TimerTickNoExpired(benchmark::State& state){
    for(auto _ : state){
        state.PauseTiming();
        Timer t;
        t.start();
        t.add(1000000, false, [](int){});
        state.ResumeTiming();
        t.handleTimerEvent();
        state.PauseTiming();
        t.stop();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TimerTickNoExpired);
