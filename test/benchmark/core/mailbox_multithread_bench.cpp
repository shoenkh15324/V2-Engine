#include <benchmark/benchmark.h>
#include "core/common/container/lock_free_mpsc_queue.hpp"

#define THREAD_MAX_NUM 128

// ── MultiPush: 다중 프로듀서 (MPSC) ──

static void MpscQueueMultiPush(benchmark::State& state){
    const int cap = static_cast<int>(state.range(0));
    static LockFreeMpscQueue<int>* mb = nullptr;
    static std::atomic<bool> ready{false};

    if(state.thread_index() == 0){
        delete mb;
        mb = new LockFreeMpscQueue<int>(cap);
        ready.store(true, std::memory_order_release);
    }else{
        while(!ready.load(std::memory_order_acquire));
    }

    int val = 42;
    int dummy;
    for(auto _ : state){
        if(!mb->push(std::move(val))){
            mb->pop(dummy);
            mb->push(std::move(val));
        }
    }
    state.SetItemsProcessed(state.iterations());
    if(state.thread_index() == 0){
        ready.store(false, std::memory_order_relaxed);
    }
}
BENCHMARK(MpscQueueMultiPush)
    ->Args({64})->Args({1024})->Args({4096})
    ->ThreadRange(1, THREAD_MAX_NUM);
