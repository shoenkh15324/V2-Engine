#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <memory>
#include "core/common/config/platform_config.h"

class IWorkDispatcher;

class Worker{
public:
    explicit Worker(IWorkDispatcher* workDispatcher, int id, int maxBatch);
    ~Worker();

    Worker(const Worker&) = delete; // std::thread 보유라 복사 금지
    Worker& operator=(const Worker&) = delete;

    void start();
    void stop();

private:
    void runLoop();

    IWorkDispatcher* workDispatcher_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int id_, maxBatch_;
};
