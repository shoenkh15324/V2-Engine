#pragma once
#include "core/common/timer/timer_base.hpp"
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"

class LinuxTimer : public TimerBase {
public:
    explicit LinuxTimer(IEventLoop* eventLoop);
    ~LinuxTimer() override;

    LinuxTimer(const LinuxTimer&) = delete;
    LinuxTimer& operator=(const LinuxTimer&) = delete;
    LinuxTimer(LinuxTimer&&) = delete;
    LinuxTimer& operator=(LinuxTimer&&) = delete;

    void start() override;
    void stop() override;

protected:
    void scheduleNextTimer(const Clock::time_point& now) override;
    void onWake() override;

private:
    IEventLoop* eventLoop_ = nullptr;
    int timerFd_ = -1;
    bool subscribed_ = false;
};
