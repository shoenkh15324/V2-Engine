#include <benchmark/benchmark.h>
#include "core/common/container/ring_buffer.hpp"
#include "core/common/util/return.hpp"
#include <vector>
#include <cstring>

// Push latency: amortized, one chunk per iteration

static void RingBufferPushLatency(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    RingBuffer buf(cap);
    std::vector<uint8_t> src(chunk, 0xAB);
    for(auto _ : state){
        if(buf.full()){
            buf.reset();
        }
        buf.push(src.data(), chunk);
    }
    state.SetBytesProcessed(state.iterations() * chunk);
}
BENCHMARK(RingBufferPushLatency)
    ->Args({1, 1})
    ->Args({3, 1})
    ->Args({7, 3})
    ->Args({64, 1})
    ->Args({64, 63})
    ->Args({64, 64})
    ->Args({256, 1})
    ->Args({256, 64})
    ->Args({256, 255})
    ->Args({4096, 1})
    ->Args({4096, 64})
    ->Args({65536, 1})
    ->Args({65536, 64})
    ->Args({65536, 65536});

// Pop latency: amortized, prefill on empty

static void RingBufferPopLatency(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    RingBuffer buf(cap);
    std::vector<uint8_t> fill(cap, 0xCD);
    std::vector<uint8_t> dst(chunk);
    for(auto _ : state){
        if(buf.empty()){
            buf.reset();
            buf.push(fill.data(), cap);
        }
        buf.pop(dst.data(), chunk);
    }
    state.SetBytesProcessed(state.iterations() * chunk);
}
BENCHMARK(RingBufferPopLatency)
    ->Args({1, 1})
    ->Args({3, 1})
    ->Args({7, 3})
    ->Args({64, 1})
    ->Args({64, 63})
    ->Args({64, 64})
    ->Args({256, 1})
    ->Args({256, 64})
    ->Args({256, 255})
    ->Args({4096, 1})
    ->Args({4096, 64})
    ->Args({65536, 1})
    ->Args({65536, 64})
    ->Args({65536, 65536});

// Push + Pop ping-pong: round-trip throughput

static void RingBufferPingPong(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    RingBuffer buf(cap);
    std::vector<uint8_t> src(chunk, 0x42);
    std::vector<uint8_t> dst(chunk);
    for(auto _ : state){
        buf.push(src.data(), chunk);
        buf.pop(dst.data(), chunk);
    }
    state.SetBytesProcessed(state.iterations() * chunk * 2);
}
BENCHMARK(RingBufferPingPong)
    ->Args({1, 1})
    ->Args({3, 1})
    ->Args({7, 3})
    ->Args({64, 1})
    ->Args({64, 63})
    ->Args({64, 64})
    ->Args({256, 1})
    ->Args({256, 64})
    ->Args({256, 255})
    ->Args({4096, 1})
    ->Args({4096, 64})
    ->Args({65536, 1})
    ->Args({65536, 64})
    ->Args({65536, 65536});

// Batch fill: push until full, then reset outside loop

static void RingBufferPushBatch(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    const size_t n = cap / chunk;
    RingBuffer buf(cap);
    std::vector<uint8_t> src(chunk, 0xAB);
    for(auto _ : state){
        for(size_t i = 0; i < n; i++){
            buf.push(src.data(), chunk);
        }
        buf.reset();
    }
    state.SetBytesProcessed(state.iterations() * n * chunk);
}
BENCHMARK(RingBufferPushBatch)
    ->Args({4, 4})
    ->Args({16, 4})
    ->Args({64, 1})
    ->Args({64, 64})
    ->Args({256, 1})
    ->Args({256, 64})
    ->Args({256, 128})
    ->Args({1024, 64})
    ->Args({4096, 64})
    ->Args({65536, 64})
    ->Args({65536, 256});

// Batch drain: pop until empty, then refill outside loop

static void RingBufferPopBatch(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    const size_t n = cap / chunk;
    RingBuffer buf(cap);
    std::vector<uint8_t> src(chunk, 0xAB);
    std::vector<uint8_t> dst(chunk);
    for(auto _ : state){
        for(size_t i = 0; i < n; i++){
            buf.push(src.data(), chunk);
        }
        for(size_t i = 0; i < n; i++){
            buf.pop(dst.data(), chunk);
        }
    }
    state.SetBytesProcessed(state.iterations() * n * chunk);
}
BENCHMARK(RingBufferPopBatch)
    ->Args({4, 4})
    ->Args({16, 4})
    ->Args({64, 1})
    ->Args({64, 64})
    ->Args({256, 1})
    ->Args({256, 64})
    ->Args({256, 128})
    ->Args({1024, 64})
    ->Args({4096, 64})
    ->Args({65536, 64})
    ->Args({65536, 256});

// Push to full buffer: return value is Fail

static void RingBufferPushFull(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    RingBuffer buf(cap);
    std::vector<uint8_t> fill(cap, 0xFF);
    std::vector<uint8_t> src(chunk, 0xAB);
    buf.push(fill.data(), cap);
    for(auto _ : state){
        buf.push(src.data(), chunk);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(RingBufferPushFull)
    ->Args({1, 1})
    ->Args({3, 1})
    ->Args({64, 1})
    ->Args({64, 64})
    ->Args({4096, 64})
    ->Args({65536, 64});

// Pop from empty buffer: return value is Fail

static void RingBufferPopEmpty(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    RingBuffer buf(cap);
    std::vector<uint8_t> dst(chunk);
    for(auto _ : state){
        buf.pop(dst.data(), chunk);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(RingBufferPopEmpty)
    ->Args({1, 1})
    ->Args({3, 1})
    ->Args({64, 1})
    ->Args({64, 64})
    ->Args({4096, 64})
    ->Args({65536, 64});

// Wrap-around stress: near-full buffer, every push wraps

static void RingBufferWrapStress(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    RingBuffer buf(cap);
    std::vector<uint8_t> src(chunk, 0xAB);
    std::vector<uint8_t> out(chunk);
    while(buf.freeSpace() > chunk * 2){
        buf.push(src.data(), chunk);
    }
    for(auto _ : state){
        buf.push(src.data(), chunk);
        buf.pop(out.data(), chunk);
    }
    state.SetBytesProcessed(state.iterations() * chunk * 2);
}
BENCHMARK(RingBufferWrapStress)
    ->Args({5, 4})
    ->Args({9, 8})
    ->Args({17, 16})
    ->Args({33, 32})
    ->Args({65, 64})
    ->Args({128, 4})
    ->Args({1024, 4})
    ->Args({65536, 4});

// Reset at half fill

static void RingBufferResetHalf(benchmark::State& state){
    const size_t cap = state.range(0);
    const size_t chunk = state.range(1);
    RingBuffer buf(cap);
    std::vector<uint8_t> src(chunk, 0x42);
    size_t half = cap / 2;
    size_t n = half / chunk;
    for(auto _ : state){
        for(size_t i = 0; i < n; i++){
            buf.push(src.data(), chunk);
        }
        buf.reset();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(RingBufferResetHalf)
    ->Args({1, 1})
    ->Args({4, 4})
    ->Args({64, 1})
    ->Args({64, 32})
    ->Args({64, 64})
    ->Args({256, 64})
    ->Args({4096, 64})
    ->Args({65536, 64});
