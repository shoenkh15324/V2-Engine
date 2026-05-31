#pragma once
#include <mutex>
#include <condition_variable>
#include "debug.hpp"

class Semaphore{
public:
    explicit Semaphore(int count = 0) : count_(count < 0 ? 0 : count){
        V2_ASSERT(count >= 0);
    }
    ~Semaphore() = default;
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&) = delete;
    Semaphore& operator=(Semaphore&&) = delete;

    void wait(){
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [this]{ return count_ > 0; });
        --count_;
    }

    void post(){
        {
            std::lock_guard<std::mutex> lock(m_);
            ++count_;
        }
        cv_.notify_one();
    }

    bool tryWait(){
        std::lock_guard<std::mutex> lock(m_);
        if(count_ > 0){
            --count_;
            return true;
        }
        return false;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    int count_;
};
