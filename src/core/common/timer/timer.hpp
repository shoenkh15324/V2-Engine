#pragma once
#include <atomic>
#include <thread>
#include <semaphore>
#include "core/common/timer/timer_base.hpp"

class Timer : public TimerBase {
public:
    Timer() = default;
    ~Timer() override;

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    void start() override;
    void stop() override;

protected:
    void scheduleNextTimer(const Clock::time_point& now) override;

private:
    void runLoop();

    std::thread thread_;
    std::binary_semaphore sema_{0};
    std::atomic<bool> running_{false};
};
