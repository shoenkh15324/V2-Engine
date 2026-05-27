#include "semaphore.hpp"

namespace osal{

Semaphore::Semaphore(int count) : count_(count < 0 ? 0 : count) {}

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

bool Semaphore::try_wait(){
    std::lock_guard<std::mutex> lock(m_);
    if(count_ > 0){
        --count_;
        return true;
    }
    return false;
}

} // namespace osal
