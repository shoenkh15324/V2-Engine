#pragma once
#include <functional>
#include "core/actor_system/runtime/dispatcher/io/i_event_loop.hpp"

class MockEventLoop : public IEventLoop {
public:
    void start() override {}
    void run() override {}
    void stop() override {}
    void post(std::function<void()>) override {}
    int subscribe(WatchedFd, Handler) override { return 0; }
    int unsubscribe(WatchedFd) override { return 0; }
};
