#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/messages/message.hpp"

class Scheduler {
public:
    Scheduler();
    ~Scheduler();
    void start();
    void stop();
    int addTimer(Actor* target, Message message, uint64_t delayMs, bool repeating = false);
    void cancel(int timerId);

private:
    using Clock = std::chrono::steady_clock;
    struct Timer{
        int id;
        Actor* target;
        Message message;
        Clock::time_point expiry;
        std::chrono::milliseconds interval;
        bool repeating;
        std::atomic<bool> canceled{false};
    };
    using TimerPtr = std::shared_ptr<Timer>;
    struct Compare{
        bool operator()(const TimerPtr& lhs, const TimerPtr& rhs) const;
    };

private:
    void threadFunc();

private:
    std::atomic<bool> running_{false};
    std::atomic<int> nextId_{0};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<TimerPtr, std::vector<TimerPtr>, Compare> queue_;
    std::unordered_map<int, TimerPtr> timers_;
};
