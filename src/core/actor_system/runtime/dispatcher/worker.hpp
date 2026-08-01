#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <memory>
#include "core/common/config/platform_config.h"

class WorkDispatcher;

class Worker{
friend class ActorSystem;
friend struct std::default_delete<Worker>;

private:
    explicit Worker(WorkDispatcher* workDispatcher, int id, int maxBatch);
    ~Worker();

    void start();
    void stop();
    void runLoop();

    WorkDispatcher* workDispatcher_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::string threadName_;
    int id_, maxBatch_;
};
