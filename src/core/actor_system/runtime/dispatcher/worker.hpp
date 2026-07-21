#pragma once
#include <atomic>
#include <thread>
#include <string>
#include "core/common/config/platform_config.h"

class IWorkDispatcher;

class Worker{
public:
    explicit Worker(IWorkDispatcher* workDispatcher, int id, int maxBatch);
    ~Worker();

    void start();
    void stop();

private:
    void runLoop();

    IWorkDispatcher* workDispatcher_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::string threadName_;
    int id_, maxBatch_;
};
