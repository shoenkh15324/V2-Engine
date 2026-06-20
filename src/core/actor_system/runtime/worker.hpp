#pragma once
#include "core/common/config.h"
#include <atomic>
#include <thread>
#include <string>

class Dispatcher;

class Worker{
public:
    explicit Worker(Dispatcher* dispatcher, int id, int maxBatch = V2_WORKER_MAX_BATCH);
    ~Worker();
    void start();
    void stop();

private:
    void runLoop();
    Dispatcher* dispatcher_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int id_, maxBatch_;
    std::string threadName_;
};
