#pragma once
#include <chrono>
#include <thread>
#include "core/common/time/time.hpp"

namespace bench {

inline constexpr uint64_t kSpinWaitTimeoutNs = 10000000000ULL; // 10초

// 완료 폴링 — totalFn()이 target에 도달하면 true.
// 폴링 사이 휴면을 넣어 카운터 캐시라인 오염(드라이버 ↔ 워커)으로 인한 측 왜곡을 저감한다.
template<typename TotalFn>
bool waitForTotal(TotalFn&& totalFn, uint64_t target, Time::TimeStamp waitStart){
    while(true){
        if(totalFn() >= target) return true;
        if(Time::toNs(Time::now() - waitStart) > kSpinWaitTimeoutNs) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

} // namespace bench
