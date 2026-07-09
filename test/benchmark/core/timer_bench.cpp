#include <benchmark/benchmark.h>
#include "core/common/time/timer.hpp"
#include <vector>
#include <cstdint>

// -- TimerAddOne ------------------------------------------------------------
// Adds a single timer into a heap that already has `prefill` timers in it.
// The prefill timers use a far-future deadline so they never fire.
// This measures the latency of add() as the heap grows.

static void TimerAddOne(benchmark::State& state){
    const int prefill = static_cast<int>(state.range(0));
    for(auto _ : state){
        Timer t;
        t.start();
        for(int i = 0; i < prefill; ++i){
            t.add(1000000, false, [](int){});
        }
        t.add(1000000, false, [](int){});
        t.stop();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TimerAddOne)
    ->Arg(0)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// -- TimerCancelOne ---------------------------------------------------------
// Adds `prefill` timers, then cancels the last one added.
// This measures the latency of cancel() as the heap grows.
// Internal note: cancel() marks the timer as dead but does NOT
// immediately remove it from the heap (lazy removal).

static void TimerCancelOne(benchmark::State& state){
    int prefill = state.range(0);
    for(auto _ : state){
        Timer t;
        t.start();
        int id;
        for(int i = 0; i < prefill; i++){
            id = t.add(1000000, false, [](int){});
        }
        t.cancel(id);
        t.stop();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TimerCancelOne)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// -- TimerDispatchMany ------------------------------------------------------
// Adds `n` zero-delay timers and dispatches them all in one shot.
// The zero-delay means each add() also checks expiry inline,
// so part of the dispatch cost is already paid during add().
// handleTimerEvent() processes whatever is left in the expiry queue.
// This measures end-to-end latency of adding + firing n timers.

static void TimerDispatchMany(benchmark::State& state){
    const int n = static_cast<int>(state.range(0));
    for(auto _ : state){
        Timer t;
        t.start();
        int count = 0;
        for(int i = 0; i < n; ++i){
            t.add(0, false, [&count](int){ ++count; });
        }
        t.handleTimerEvent();
        t.stop();
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(TimerDispatchMany)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// -- TimerCheckPending ------------------------------------------------------
// Registers a single repeating timer (1µs delay) and calls handleTimerEvent()
// before it expires. The timer stays in the heap; only the
// "top-of-heap not yet due" fast path is exercised.
// This measures the cost of a no-op dispatch check.

static void TimerCheckPending(benchmark::State& state){
    for(auto _ : state){
        Timer t;
        t.start();
        int count = 0;
        t.add(1, true, [&count](int){ ++count; });
        t.handleTimerEvent();
        t.stop();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TimerCheckPending);

// -- TimerClearMany ---------------------------------------------------------
// Adds `n` far-future timers then clears them all at once.
// clear() resets the heap and disarms the kernel timerfd.
// This measures the cost of bulk teardown for n timers.

static void TimerClearMany(benchmark::State& state){
    const int n = static_cast<int>(state.range(0));
    for(auto _ : state){
        Timer t;
        t.start();
        for(int i = 0; i < n; ++i){
            t.add(1000000, false, [](int){});
        }
        t.clear();
        t.stop();
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(TimerClearMany)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// -- TimerAddMany -----------------------------------------------------------
// Adds `n` far-future timers in sequence, then stops the timer.
// stop() disarms the kernel timerfd.  The heap is destroyed when
// the Timer goes out of scope (destructor).
// This measures the raw throughput of add() for a batch.

static void TimerAddMany(benchmark::State& state){
    const int n = static_cast<int>(state.range(0));
    for(auto _ : state){
        Timer t;
        t.start();
        for(int i = 0; i < n; ++i){
            t.add(1000000, false, [](int){});
        }
        t.stop();
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(TimerAddMany)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// -- TimerMixedWorkload -----------------------------------------------------
// Simulates a realistic tick: 60% add + 30% cancel + 10% fire.
// The ratio approximates a busy actor tick where most timers are
// non-expired, some are cancelled, and a few fire immediately.

static void TimerMixedWorkload(benchmark::State& state){
    const int n = static_cast<int>(state.range(0));
    const int addCount = n * 60 / 100;
    const int cancelCount = n * 30 / 100;
    for(auto _ : state){
        Timer t;
        t.start();
        std::vector<int> ids;
        ids.reserve(addCount);
        for(int i = 0; i < addCount; ++i){
            const bool repeating = (i % 20 == 0);
            const uint64_t delay = (i % 5 == 0) ? 0 : 1000000;
            ids.push_back(t.add(delay, repeating, [](int){}));
        }
        for(int i = 0; i < cancelCount && i < static_cast<int>(ids.size()); ++i){
            t.cancel(ids[i]);
        }
        t.handleTimerEvent();
        t.stop();
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(TimerMixedWorkload)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

// -- TimerCheckIdle ---------------------------------------------------------
// Registers a single far-future timer and calls handleTimerEvent().
// Since the timer is nowhere near expired, the fast path
// (read top-of-heap, compare with now, return) is always taken.
// This measures the fastest possible dispatch path.

static void TimerCheckIdle(benchmark::State& state){
    for(auto _ : state){
        Timer t;
        t.start();
        t.add(1000000, false, [](int){});
        t.handleTimerEvent();
        t.stop();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TimerCheckIdle);
