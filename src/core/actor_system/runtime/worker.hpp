#pragma once
#include <atomic>
#include <memory>
#include "core/osal/thread/thread.hpp"

class Dispatcher;

class Worker{
public:
    explicit Worker(Dispatcher* dispatcher, int maxBatch = -1);
    ~Worker();
    void start();
    void stop();

private:
    static void runLoop(void* arg);
    Dispatcher* dispatcher_;
    std::unique_ptr<Thread> thread_;
    std::atomic<bool> running_{false};
    int maxBatch_;
};
