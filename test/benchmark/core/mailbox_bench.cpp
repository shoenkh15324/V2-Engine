#include <benchmark/benchmark.h>
#include "core/actor_system/runtime/mailbox.hpp"

// Push latency: amortized, one push per iteration

static void MailboxPushLatency(benchmark::State& state){
    int cap = state.range(0);
    Mailbox<int> mb(cap);
    int val = 42;
    int dummy;
    for(auto _ : state){
        if(!mb.push(val)){
            mb.pop(dummy);
            mb.push(val);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MailboxPushLatency)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(5)
    ->Arg(7)
    ->Arg(16)
    ->Arg(31)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(4096);

// Pop latency: amortized, one pop per iteration

static void MailboxPopLatency(benchmark::State& state){
    int cap = state.range(0);
    Mailbox<int> mb(cap);
    int out;
    for(auto _ : state){
        if(!mb.pop(out)){
            for(int i = 0; i < cap; i++){
                mb.push(i);
            }
            mb.pop(out);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MailboxPopLatency)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(5)
    ->Arg(7)
    ->Arg(16)
    ->Arg(31)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(4096);

// Push + Pop ping-pong: round-trip throughput

static void MailboxPingPong(benchmark::State& state){
    int cap = state.range(0);
    Mailbox<int> mb(cap);
    int out;
    for(auto _ : state){
        mb.push(42);
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(MailboxPingPong)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(5)
    ->Arg(7)
    ->Arg(16)
    ->Arg(31)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(4096);

// Push to full buffer: returns false

static void MailboxPushFull(benchmark::State& state){
    int cap = state.range(0);
    Mailbox<int> mb(cap);
    for(int i = 0; i < cap; i++){
        mb.push(i);
    }
    for(auto _ : state){
        mb.push(42);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MailboxPushFull)
    ->Arg(1)
    ->Arg(3)
    ->Arg(64)
    ->Arg(1024)
    ->Arg(4096);

// Pop from empty buffer: returns false

static void MailboxPopEmpty(benchmark::State& state){
    Mailbox<int> mb(state.range(0));
    int out;
    for(auto _ : state){
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MailboxPopEmpty)
    ->Arg(1)
    ->Arg(3)
    ->Arg(64)
    ->Arg(1024)
    ->Arg(4096);

// Batch fill: fill, then drain

static void MailboxFillBatch(benchmark::State& state){
    int cap = state.range(0);
    Mailbox<int> mb(cap);
    int dummy;
    for(auto _ : state){
        for(int i = 0; i < cap; i++){
            mb.push(i);
        }
        while(mb.pop(dummy));
    }
    state.SetItemsProcessed(state.iterations() * cap);
}
BENCHMARK(MailboxFillBatch)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(5)
    ->Arg(7)
    ->Arg(16)
    ->Arg(31)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(4096);

// Batch drain: fill, then drain

static void MailboxDrainBatch(benchmark::State& state){
    int cap = state.range(0);
    Mailbox<int> mb(cap);
    int out;
    for(auto _ : state){
        for(int i = 0; i < cap; i++){
            mb.push(i);
        }
        for(int i = 0; i < cap; i++){
            mb.pop(out);
        }
    }
    state.SetItemsProcessed(state.iterations() * cap);
}
BENCHMARK(MailboxDrainBatch)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(5)
    ->Arg(7)
    ->Arg(16)
    ->Arg(31)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(4096);

// Wrap-around: near-full buffer, push+pop cycle wraps tail

static void MailboxWrapAround(benchmark::State& state){
    int cap = state.range(0);
    Mailbox<int> mb(cap);
    for(int i = 0; i < cap - 1; i++){
        mb.push(i);
    }
    int out;
    for(auto _ : state){
        mb.push(42);
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(MailboxWrapAround)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(5)
    ->Arg(7)
    ->Arg(16)
    ->Arg(31)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024);
