#pragma once
#include <mutex>
#include <queue>
#include <atomic>
#include <chrono>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "core/common/timer/i_timer.hpp"

class TimerBase : public ITimer {
public:
    using Clock = std::chrono::steady_clock;

    TimerBase() = default;
    ~TimerBase() override = default;

    TimerBase(const TimerBase&) = delete;
    TimerBase& operator=(const TimerBase&) = delete;
    TimerBase(TimerBase&&) = delete;
    TimerBase& operator=(TimerBase&&) = delete;

    int add(uint64_t delayMs, bool repeating, Callback cb, void* payload) override;
    void cancel(int id) override;
    void clear() override;
    bool isAlive(int id) const override;
    bool isRepeating(int id) const override;
    void handleTimerEvent() override final;

protected:
    void start() override = 0;
    void stop() override = 0;

    void purgeDeadEntries();
    bool hasLiveTop() const;
    Clock::time_point nextExpiry() const;
    void executeExpiredTimers();
    
    virtual void scheduleNextTimer(const Clock::time_point& now) = 0;
    virtual void onWake(){}

    mutable std::mutex mutex_;

private:
    struct TimerNode {
        Clock::time_point expiry;
        std::chrono::milliseconds interval;
        Callback cb = nullptr;
        void* payload = nullptr;
        int id = -1;
        bool repeating = false;
        bool alive = true;
    };

    struct Compare { // 인덱스 기반 최소힙 비교자
        const std::vector<TimerNode>* pool = nullptr;
        bool operator()(int a,  int b) const {
            return (*pool)[a].expiry > (*pool)[b].expiry;
        }
    };

    int allocNode();
    void freeNode(int idx);

    std::atomic<int> nextId_{1};
    std::vector<int> freeList_;
    std::vector<TimerNode> pool_;
    std::unordered_map<int, int> timers_; // id -> pool_ 인덱스
    std::priority_queue<int, std::vector<int>, Compare> heap_{Compare{{&pool_}}};
};
