#include "semaphore.hpp"

namespace osal{

Semaphore::Semaphore(int count) : count_(count) {};

void Semaphore::wait(){
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait(lock, [this]{
        return count_ > 0;
    });
    --count_;
}

void Semaphore::post(){
    {
        std::unique_lock<std::mutex> lock(m_);
        ++count_;
    }
    cv_.notify_one();
}

} // namespace osal
