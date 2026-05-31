#pragma once
#include <atomic>
#include <thread>

class Dispatcher;

class Worker{
public:
    explicit Worker(Dispatcher* dispatcher, int maxBatch = -1);
    ~Worker();
    void start();
    void stop();

private:
    void runLoop();
    Dispatcher* dispatcher_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int maxBatch_;
};
