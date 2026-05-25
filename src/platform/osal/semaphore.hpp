#pragma once

#include <mutex>
#include <condition_variable>

class Semaphore{
public:
    explicit Semaphore(int count = 0) : count_(count){}
    ~Semaphore() = default;
    void wait(){
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [this]{
            return count_ > 0;
        });
        --count_;
    }
    void post(){
        std::unique_lock<std::mutex> lock(m_);
        ++count_;
        cv_.notify_one();
    }
    bool try_wait(){
        std::unique_lock<std::mutex> lock(m_);
        if(count_ > 0){
            --count_;
            return true;
        }else
            return false;
    }
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
private:
    std::mutex m_;
    std::condition_variable cv_;
    int count_;
};
