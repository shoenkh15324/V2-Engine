#include <benchmark/benchmark.h>
#include "core/actor_system/runtime/mailbox.hpp"

// Push throughput

static void MailboxPush(benchmark::State& state){
    Mailbox<int> mb(state.range(0));
    int val = 42;
    int dummy;
    for(auto _ : state){
        if(!mb.push(val)){
            do{
                mb.pop(dummy);
            }while(!mb.push(val));
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MailboxPush)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(16384)
    ->Arg(65536)
    ->Arg(262144)
    ->Arg(1048576);

// Pop throughput

static void MailboxPop(benchmark::State& state){
    Mailbox<int> mb(state.range(0));
    int val;
    for(auto _ : state){
        if(!mb.pop(val)){
            for(int i = 0; i < state.range(0); i++){
                mb.push(i);
            }
            mb.pop(val);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(MailboxPop)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(16384)
    ->Arg(65536)
    ->Arg(262144)
    ->Arg(1048576);

// Push + Pop ping-pong

static void MailboxPingPong(benchmark::State& state){
    Mailbox<int> mb(state.range(0));
    int val = 42;
    int out;
    for(auto _ : state){
        mb.push(val);
        mb.pop(out);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(MailboxPingPong)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(16384)
    ->Arg(65536)
    ->Arg(262144)
    ->Arg(1048576);

// Full Buffer Cycle

static void MailboxFullCycle(benchmark::State& state){
    Mailbox<int> mb(state.range(0));
    int val = 42;
    int out;
    for(auto _ : state){
        for(int i = 0; i < state.range(0); i++){
            mb.push(val + i);
        }
        for(int i = 0; i < state.range(0); i++){
            mb.pop(out);
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0) * 2); 
}
BENCHMARK(MailboxFullCycle)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(16384)
    ->Arg(65536)
    ->Arg(262144);
