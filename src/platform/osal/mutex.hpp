#pragma once

#include <mutex>

class Mutex{
public:
    void lock(){ m_.lock(); }
    void unlock(){ m_.unlock(); }
    bool try_lock() { return m_.try_lock(); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
private:
    std::mutex m_;
};
