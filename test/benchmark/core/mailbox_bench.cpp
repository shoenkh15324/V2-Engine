#include <benchmark/benchmark.h>
#include "core/common/container/lock_free_mpsc_queue.hpp"

// ── Push latency ──

static void MpscQueuePushLatency(benchmark::State& state){
    int cap = state.range(0);
    LockFreeMpscQueue<int> mb(cap);
    int val = 42;
    int dummy;
    for(auto _ : state){
        if(!mb.push(std::move(val))){
            mb.pop(dummy);
            mb.push(std::move(val));
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MpscQueuePushLatency)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Pop latency ──

static void MpscQueuePopLatency(benchmark::State& state){
    int cap = state.range(0);
    LockFreeMpscQueue<int> mb(cap);
    int out;
    for(auto _ : state){
        if(!mb.pop(out)){
            for(int i = 0; i < cap; i++){
                mb.push(std::move(i));
            }
            mb.pop(out);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MpscQueuePopLatency)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Push + Pop ping-pong ──

static void MpscQueuePingPong(benchmark::State& state){
    int cap = state.range(0);
    LockFreeMpscQueue<int> mb(cap);
    int out;
    for(auto _ : state){
        mb.push(42);
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(MpscQueuePingPong)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Push to full buffer ──

static void MpscQueuePushFull(benchmark::State& state){
    int cap = state.range(0);
    LockFreeMpscQueue<int> mb(cap);
    for(int i = 0; i < cap; i++){
        mb.push(std::move(i));
    }
    for(auto _ : state){
        mb.push(42);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MpscQueuePushFull)
    ->Arg(1)->Arg(3)->Arg(64)->Arg(1024)->Arg(4096);

// ── Pop from empty buffer ──

static void MpscQueuePopEmpty(benchmark::State& state){
    LockFreeMpscQueue<int> mb(state.range(0));
    int out;
    for(auto _ : state){
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MpscQueuePopEmpty)
    ->Arg(1)->Arg(3)->Arg(64)->Arg(1024)->Arg(4096);

// ── Batch fill ──

static void MpscQueueFillBatch(benchmark::State& state){
    int cap = state.range(0);
    LockFreeMpscQueue<int> mb(cap);
    int dummy;
    for(auto _ : state){
        for(int i = 0; i < cap; i++){
            mb.push(std::move(i));
        }
        while(mb.pop(dummy));
    }
    state.SetItemsProcessed(state.iterations() * cap);
}
BENCHMARK(MpscQueueFillBatch)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Batch drain ──

static void MpscQueueDrainBatch(benchmark::State& state){
    int cap = state.range(0);
    LockFreeMpscQueue<int> mb(cap);
    int out;
    for(auto _ : state){
        for(int i = 0; i < cap; i++){
            mb.push(std::move(i));
        }
        for(int i = 0; i < cap; i++){
            mb.pop(out);
        }
    }
    state.SetItemsProcessed(state.iterations() * cap);
}
BENCHMARK(MpscQueueDrainBatch)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Wrap-around ──

static void MpscQueueWrapAround(benchmark::State& state){
    int cap = state.range(0);
    LockFreeMpscQueue<int> mb(cap);
    for(int i = 0; i < cap - 1; i++){
        mb.push(std::move(i));
    }
    int out;
    for(auto _ : state){
        mb.push(42);
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(MpscQueueWrapAround)
    ->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);
