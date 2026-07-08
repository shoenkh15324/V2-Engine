#include <benchmark/benchmark.h>
#include "core/common/container/ring_buffer.hpp"
#include <vector>
#include <cstring>

// Push throughput

static void RingBufferPush(benchmark::State& state){
    RingBuffer buf(state.range(0));
    size_t chunk = state.range(1);
    std::vector<uint8_t> src(chunk, 0xAB);
    for(auto _ : state){
        if(buf.freeSpace() < chunk) buf.reset();
        buf.push(src.data(), chunk);
    }
    state.SetBytesProcessed(state.iterations() * chunk);
}
BENCHMARK(RingBufferPush)
    // boundary: tiny capacity
    ->Args({1, 1})
    ->Args({2, 1})
    ->Args({4, 4})
    ->Args({8, 4})
    ->Args({8, 8})
    // normal → large capacity, small chunk
    ->Args({256, 1})
    ->Args({256, 8})
    ->Args({256, 64})
    ->Args({4096, 64})
    ->Args({65536, 64})
    // huge capacity + tiny chunk: ptr arithmetic + cache miss stress
    ->Args({1048576, 1})
    ->Args({16777216, 1})
    ->Args({268435456, 1})
    // huge capacity + medium chunk
    ->Args({1048576, 64})
    ->Args({16777216, 64})
    ->Args({268435456, 64})
    // large chunk: memcpy bandwidth
    ->Args({1048576, 65536})
    ->Args({16777216, 65536})
    ->Args({268435456, 65536})
    // max chunk = capacity: full buffer single push
    ->Args({65536, 65536})
    ->Args({1048576, 1048576});

// Pop throughput

static void RingBufferPop(benchmark::State& state){
    RingBuffer buf(state.range(0));
    size_t chunk = state.range(1);
    std::vector<uint8_t> data(chunk, 0xCD);
    std::vector<uint8_t> dst(chunk);
    for(auto _ : state){
        if(buf.empty()){
            buf.reset();
            buf.push(data.data(), chunk);
        }
        buf.pop(dst.data(), chunk);
    }
    state.SetBytesProcessed(state.iterations() * chunk);
}
BENCHMARK(RingBufferPop)
    ->Args({256, 4})
    ->Args({256, 64})
    ->Args({1024, 64})
    ->Args({65536, 64})
    ->Args({1048576, 64})
    ->Args({1048576, 65536})
    ->Args({16777216, 65536});

// Push+Pop ping-pong

static void RingBufferPingPong(benchmark::State& state){
    RingBuffer buf(state.range(0));
    size_t chunk = state.range(1);
    std::vector<uint8_t> data(chunk, 0x42);
    std::vector<uint8_t> out(chunk);
    for(auto _ : state){
        buf.push(data.data(), chunk);
        buf.pop(out.data(), chunk);
    }
    state.SetBytesProcessed(state.iterations() * chunk * 2);
}
BENCHMARK(RingBufferPingPong)
    ->Args({256, 8})
    ->Args({256, 64})
    ->Args({1024, 256})
    ->Args({65536, 64})
    ->Args({1048576, 1024})
    ->Args({1048576, 65536});

// Worst-case wrap stress

static void RingBufferWrapStress(benchmark::State& state){
    RingBuffer buf(state.range(0));
    size_t chunk = state.range(1);
    std::vector<uint8_t> data(chunk, 0xAB);
    std::vector<uint8_t> out(chunk);
    // fill to near-full so every push wraps
    while(buf.freeSpace() > chunk * 2){
        buf.push(data.data(), chunk);
    }
    for(auto _ : state){
        buf.push(data.data(), chunk);
        buf.pop(out.data(), chunk);
    }
    state.SetBytesProcessed(state.iterations() * chunk * 2);
}
BENCHMARK(RingBufferWrapStress)
    // tight: capacity barely > chunk
    ->Args({5, 4})
    ->Args({9, 8})
    ->Args({17, 16})
    // moderate wrap
    ->Args({128, 4})
    ->Args({1024, 4})
    // large buffer, small chunk → many wraps
    ->Args({65536, 4})
    ->Args({1048576, 4});

// Reset latency

static void RingBufferReset(benchmark::State& state){
    RingBuffer buf(state.range(0));
    size_t chunk = state.range(1);
    std::vector<uint8_t> data(chunk, 0xFF);
    for(auto _ : state){
        buf.push(data.data(), chunk);
        buf.reset();
    }
}
BENCHMARK(RingBufferReset)
    ->Args({256, 64})
    ->Args({65536, 64})
    ->Args({1048576, 65536})
    ->Args({16777216, 65536});
