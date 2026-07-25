#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include "core/common/time/timer.hpp"
#include "core/actor_system/runtime/i_scheduler.hpp"

class IEventLoop;

class Scheduler : public IScheduler{
public:
    Scheduler() = default;
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    void start(IEventLoop* eventLoop = nullptr);
    void stop();
    int addTimer(IActorRuntime* target, Message msg, uint64_t timeMs, bool repeating = false) override;
    void cancel(int id) override;

private:
    struct TimerCtx{
        IActorRuntime* target;
        Message msg;
    };

    void subscribeTimer();
    void unsubscribeTimer();
    void cleanupTimerCtxs();
    static void timerCallback(int id, void* ctx);

    Timer timer_;
    IEventLoop* eventLoop_ = nullptr;
    std::unordered_map<int, std::unique_ptr<TimerCtx>> timerCtxs_;
};
