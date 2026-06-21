#pragma once
#include <cstdint>
#include <memory>
#include "core/common/timer.hpp"
#include "core/actor_system/runtime/i_scheduler.hpp"

class Dispatcher;

class Scheduler : public IScheduler{
public:
    Scheduler() = default;
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    void start(Dispatcher* dispatcher = nullptr);
    void stop();
    int addTimer(Actor* target, Message message, uint64_t timeMs, bool repeating = false) override;
    void cancel(int timerId) override;

private:
    void subscribeTimer();
    void unsubscribeTimer();

    Timer timer_;
    Dispatcher* dispatcher_ = nullptr;
};
