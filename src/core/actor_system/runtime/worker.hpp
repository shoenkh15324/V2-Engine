#pragma once
#include "core/common/config/platform_config.h"
#include <atomic>
#include <thread>
#include <string>

class Dispatcher;

class Worker{
public:
    explicit Worker(Dispatcher* dispatcher, int id, int maxBatch);
    ~Worker();

    void start();
    void stop();

private:
    void runLoop();

    Dispatcher* dispatcher_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::string threadName_;
    int id_, maxBatch_;
};
