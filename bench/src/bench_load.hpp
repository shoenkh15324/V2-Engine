#pragma once
#include "bench_throughput.hpp"
#include "core/common/time/time.hpp"
#include "service/tick/tick_messages.hpp"
#include <atomic>
#include <thread>
#include <vector>

namespace bench {

// 프로듀서 스레드를 띄우고 go 배리어로 동시 방출한 뒤 join한다.
// 스레드 생성 비용은 측정 구간 밖(#6). 발행 구간은 프로듀서별 연속 분할 +
// 전체 액터 라운드로빈(시작 오프셋 상이 → 자연 디동기화).
// 측정 시작 시각을 반환한다.
inline Time::TimeStamp publishMessages(std::vector<BenchActor*>& acts, int producers, int64_t iterations){
    const int64_t per = iterations / producers;
    const int64_t rem = iterations % producers;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for(int k = 0; k < producers; ++k){
        threads.emplace_back([&, k]{
            while(!go.load(std::memory_order_acquire)) std::this_thread::yield();
            const int64_t begin = static_cast<int64_t>(k) * per + std::min<int64_t>(k, rem);
            const int64_t count = per + ((k < rem) ? 1 : 0);
            for(int64_t n = 0; n < count; ++n){
                acts[static_cast<size_t>((begin + n) % static_cast<int64_t>(acts.size()))]->receiveMsg(Message::make(Tick{}));
            }
        });
    }
    auto start = Time::now();
    go.store(true, std::memory_order_release);
    for(auto& t : threads) t.join();
    return start;
}

inline uint64_t totalProcessed(const std::vector<BenchActor*>& acts){
    uint64_t total = 0;
    for(auto* a : acts) total += a->processed();
    return total;
}

} // namespace bench
