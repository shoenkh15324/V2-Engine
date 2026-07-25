#pragma once
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "core/common/config/platform_config.h"

class Timer{
public:
    using Clock = std::chrono::steady_clock;
    using Callback = void(*)(int, void*);

    Timer();
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    int add(uint64_t delayMs, bool repeating, Callback cb, void* payload);
    void cancel(int id);
    void clear();
    void start();
    void stop();
    void handleTimerEvent();
    bool isRepeating(int id) const;
    bool isAlive(int id) const;

#if V2_PLATFORM_LINUX
    int fd() const;
#endif

private:
    struct TimerNode{
        Clock::time_point expiry;
        std::chrono::milliseconds interval;
        Callback cb = nullptr;
        void* payload = nullptr;
        int id = -1;
        bool repeating = false;
        bool alive = true;
    };

    struct Compare{ // 인덱스 기반 비교 함수
        const std::vector<TimerNode>* pool = nullptr;
        bool operator()(int a, int b) const {
            return (*pool)[a].expiry > (*pool)[b].expiry;
        }
    };

    int allocNode(); // 슬롯 할당 (free list 우선)
    void freeNode(int idx); // 슬롯 해제 (free list에 추가)
    void excuteExpiredTimers();
    void scheduleNextTimer(const Clock::time_point& now);

    Compare compare_{&pool_};
    mutable std::mutex mutex_;
    std::atomic<int> nextId_{1};
    std::vector<int> freeList_; // 재사용 가능 슬롯 인덱스
    std::vector<TimerNode> pool_;
    std::unordered_map<int, int> timers_; // id -> pool_ 인덱스
    std::priority_queue<int, std::vector<int>, Compare> heap_{compare_}; // 인덱스 저장

#if V2_PLATFORM_LINUX
    int timerFd_ = -1;
#else
    std::thread thread_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
#endif
};
