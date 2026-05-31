#include "semaphore.hpp"
#include "../../../core/common/debug.hpp"

Semaphore::Semaphore(int count) : count_(count < 0 ? 0 : count){
    V2_ASSERT(count >= 0);
}

void Semaphore::wait(){
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait(lock, [this]{
        return count_ > 0;
    });
    --count_;
}

void Semaphore::post(){
    {
        std::lock_guard<std::mutex> lock(m_);
        ++count_;
    }
    cv_.notify_one();
}

bool Semaphore::tryWait(){
    std::lock_guard<std::mutex> lock(m_);
    if(count_ > 0){
        --count_;
        return true;
    }
    return false;
}
