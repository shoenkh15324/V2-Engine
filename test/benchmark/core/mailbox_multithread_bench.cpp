#include <benchmark/benchmark.h>
#include "core/actor_system/runtime/mailbox_mutex.hpp"
#include "core/actor_system/runtime/mailbox_lockfree.hpp"

#define THREAD_MAX_NUM 128

// ── MultiPush: 다중 프로듀서 (MPSC 둘 다 지원) ──

template <typename MailboxT>
static void MailboxMultiPush(benchmark::State& state){
    const int cap = static_cast<int>(state.range(0));
    static MailboxT* mb = nullptr;
    static std::atomic<bool> ready{false};

    if(state.thread_index() == 0){
        delete mb;
        mb = new MailboxT(cap);
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
BENCHMARK (MailboxMultiPush<MutexMailbox<int>>)
    ->Args({64})->Args({1024})->Args({4096})
    ->ThreadRange(1, THREAD_MAX_NUM);
BENCHMARK (MailboxMultiPush<LockFreeMailbox<int>>)
    ->Args({64})->Args({1024})->Args({4096})
    ->ThreadRange(1, THREAD_MAX_NUM);

// ── MultiPop: 다중 컨슈머 (뮤텍스만) ──

static void MailboxMultiPop(benchmark::State& state){
    const int cap = static_cast<int>(state.range(0));
    static MutexMailbox<int>* mb = nullptr;
    static std::atomic<bool> ready{false};

    if(state.thread_index() == 0){
        delete mb;
        mb = new MutexMailbox<int>(cap);
        ready.store(true, std::memory_order_release);
    }else{
        while(!ready.load(std::memory_order_acquire));
    }

    int out;
    for(auto _ : state){
        if(!mb->pop(out)){
            mb->push(42);
            mb->pop(out);
        }
    }
    state.SetItemsProcessed(state.iterations());
    if(state.thread_index() == 0){
        ready.store(false, std::memory_order_relaxed);
    }
}
BENCHMARK(MailboxMultiPop)
    ->Args({64})->Args({1024})->Args({4096})
    ->ThreadRange(1, THREAD_MAX_NUM);

// ── MultiHalf: 절반 프로듀서 + 절반 컨슈머 (뮤텍스만) ──

static void MailboxMultiHalf(benchmark::State& state){
    const int cap = static_cast<int>(state.range(0));
    static MutexMailbox<int>* mb = nullptr;
    static std::atomic<bool> ready{false};

    if(state.thread_index() == 0){
        delete mb;
        mb = new MutexMailbox<int>(cap);
        ready.store(true, std::memory_order_release);
    }else{
        while(!ready.load(std::memory_order_acquire));
    }

    bool isProducer = (state.thread_index() % 2 == 0);
    int val = 42;
    int dummy;
    for(auto _ : state){
        if(isProducer){
            if(!mb->push(std::move(val))){
                mb->pop(dummy);
                mb->pop(val);
            }
        }else{
            if(!mb->pop(dummy)){
                mb->push(42);
                mb->pop(dummy);
            }
        }
    }
    state.SetItemsProcessed(state.iterations());
    if(state.thread_index() == 0){
        ready.store(false, std::memory_order_relaxed);
    }
}
BENCHMARK(MailboxMultiHalf)
    ->Args({64})->Args({1024})->Args({4096})
    ->ThreadRange(2, THREAD_MAX_NUM);

// ── MultiPingPong: 각 스레드가 push+pop (뮤텍스만) ──

static void MailboxMultiPingPong(benchmark::State& state){
    const int cap = static_cast<int>(state.range(0));
    static MutexMailbox<int>* mb = nullptr;
    static std::atomic<bool> ready{false};

    if(state.thread_index() == 0){
        delete mb;
        mb = new MutexMailbox<int>(cap);
        ready.store(true, std::memory_order_release);
    }else{
        while(!ready.load(std::memory_order_acquire));
    }

    int val = 42;
    int out;
    for(auto _ : state){
        do{
            if(mb->push(std::move(val))) break;
            mb->pop(out);
        }while(true);
        do{
            if(mb->pop(out)) break;
            mb->push(std::move(val));
        }while(true);
    }
    state.SetItemsProcessed(state.iterations() * 2);
    if(state.thread_index() == 0){
        ready.store(false, std::memory_order_relaxed);
    }
}
BENCHMARK(MailboxMultiPingPong)
    ->Args({64})->Args({1024})->Args({4096})
    ->ThreadRange(1, THREAD_MAX_NUM);
