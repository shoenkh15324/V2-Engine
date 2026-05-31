#pragma once
#include <mutex>

class Mutex{
public:
    Mutex() = default;
    ~Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;
    void lock();
    void unlock();
    bool tryLock();
    std::mutex& native();

private:
    std::mutex m_;
};
