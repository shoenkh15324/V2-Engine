#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "core/common/timer/timer.hpp"
#include "core/actor_system/runtime/scheduler/i_scheduler.hpp"

class Scheduler : public IScheduler{
public:
    explicit Scheduler(std::unique_ptr<ITimer> timer = nullptr);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    void start() override;
    void stop() override;
    int addTimer(IActorRuntime* target, Message msg, uint64_t delayMs, bool repeating = false) override;
    void cancel(int id) override;

private:
    struct TimerCtx{
        IActorRuntime* target;
        Message msg;
    };

    void cleanupTimerCtxs();
    static void timerCallback(int id, void* ctx);

    std::mutex mutex_;
    std::unique_ptr<ITimer> timer_;
    std::unordered_map<int, std::unique_ptr<TimerCtx>> timerCtxs_;
};
