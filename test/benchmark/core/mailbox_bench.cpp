#include <benchmark/benchmark.h>
#include "core/actor_system/runtime/mailbox_mutex.hpp"
#include "core/actor_system/runtime/mailbox_lockfree.hpp"

// ── Push latency ──

template <typename MailboxT>
static void MailboxPushLatency(benchmark::State& state){
    int cap = state.range(0);
    MailboxT mb(cap);
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
BENCHMARK (MailboxPushLatency<MutexMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);
BENCHMARK (MailboxPushLatency<LockFreeMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Pop latency ──

template <typename MailboxT>
static void MailboxPopLatency(benchmark::State& state){
    int cap = state.range(0);
    MailboxT mb(cap);
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
BENCHMARK (MailboxPopLatency<MutexMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);
BENCHMARK (MailboxPopLatency<LockFreeMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Push + Pop ping-pong ──

template <typename MailboxT>
static void MailboxPingPong(benchmark::State& state){
    int cap = state.range(0);
    MailboxT mb(cap);
    int out;
    for(auto _ : state){
        mb.push(42);
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK (MailboxPingPong<MutexMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);
BENCHMARK (MailboxPingPong<LockFreeMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Push to full buffer ──

template <typename MailboxT>
static void MailboxPushFull(benchmark::State& state){
    int cap = state.range(0);
    MailboxT mb(cap);
    for(int i = 0; i < cap; i++){
        mb.push(std::move(i));
    }
    for(auto _ : state){
        mb.push(42);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK (MailboxPushFull<MutexMailbox<int>>)
    ->Arg(1)->Arg(3)->Arg(64)->Arg(1024)->Arg(4096);
BENCHMARK (MailboxPushFull<LockFreeMailbox<int>>)
    ->Arg(1)->Arg(3)->Arg(64)->Arg(1024)->Arg(4096);

// ── Pop from empty buffer ──

template <typename MailboxT>
static void MailboxPopEmpty(benchmark::State& state){
    MailboxT mb(state.range(0));
    int out;
    for(auto _ : state){
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK (MailboxPopEmpty<MutexMailbox<int>>)
    ->Arg(1)->Arg(3)->Arg(64)->Arg(1024)->Arg(4096);
BENCHMARK (MailboxPopEmpty<LockFreeMailbox<int>>)
    ->Arg(1)->Arg(3)->Arg(64)->Arg(1024)->Arg(4096);

// ── Batch fill ──

template <typename MailboxT>
static void MailboxFillBatch(benchmark::State& state){
    int cap = state.range(0);
    MailboxT mb(cap);
    int dummy;
    for(auto _ : state){
        for(int i = 0; i < cap; i++){
            mb.push(std::move(i));
        }
        while(mb.pop(dummy));
    }
    state.SetItemsProcessed(state.iterations() * cap);
}
BENCHMARK (MailboxFillBatch<MutexMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);
BENCHMARK (MailboxFillBatch<LockFreeMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Batch drain ──

template <typename MailboxT>
static void MailboxDrainBatch(benchmark::State& state){
    int cap = state.range(0);
    MailboxT mb(cap);
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
BENCHMARK (MailboxDrainBatch<MutexMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);
BENCHMARK (MailboxDrainBatch<LockFreeMailbox<int>>)
    ->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024)->Arg(4096);

// ── Wrap-around ──

template <typename MailboxT>
static void MailboxWrapAround(benchmark::State& state){
    int cap = state.range(0);
    MailboxT mb(cap);
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
BENCHMARK (MailboxWrapAround<MutexMailbox<int>>)
    ->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);
BENCHMARK (MailboxWrapAround<LockFreeMailbox<int>>)
    ->Arg(2)->Arg(3)->Arg(4)->Arg(5)->Arg(7)
    ->Arg(16)->Arg(31)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);
