#pragma once
#include "core/common/config.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

class Timer {
public:
    using Clock = std::chrono::steady_clock;
    using Callback = std::function<void(int)>;

    Timer();
    ~Timer();
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    int add(uint64_t delayMs, bool repeating, Callback cb);
    void cancel(int id);
    void clear();
    void start();
    void stop();

    int fd() const;
    void onTick();

private:
    struct TimerNode{
        int id;
        Clock::time_point expiry;
        std::chrono::milliseconds interval;
        bool repeating;
        bool alive = true;
        Callback cb;
    };
    using TimerPtr = std::shared_ptr<TimerNode>;
    struct Compare{
        bool operator()(const TimerPtr& a, const TimerPtr& b) const {
            return a->expiry > b->expiry;
        }
    };

    void fire();
    void rearm();

    std::priority_queue<TimerPtr, std::vector<TimerPtr>, Compare> heap_;
    std::unordered_map<int, TimerPtr> timers_;
    mutable std::mutex mutex_;
    std::atomic<int> nextId_{1};

#if V2_PLATFORM_LINUX
    int timerFd_ = -1;
#else
    std::thread thread_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
#endif
};
