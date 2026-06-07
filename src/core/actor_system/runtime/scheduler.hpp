#pragma once
#include <cstdint>
#include <memory>
#include "core/common/timer.hpp"
#include "core/actor_system/messages/message.hpp"

class Actor;
class Dispatcher;

class Scheduler
{
public:
    Scheduler();
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    void start(Dispatcher* dispatcher = nullptr);
    void stop();
    int addTimer(Actor* target, Message message, uint64_t delayMs, bool repeating = false);
    void cancel(int timerId);

private:
    Timer timer_;
    Dispatcher* dispatcher_ = nullptr;
};
