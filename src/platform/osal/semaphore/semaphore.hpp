#pragma once
#include <mutex>
#include <condition_variable>

namespace osal{

class Semaphore{
public:
    explicit Semaphore(int count = 0);
    ~Semaphore() = default;
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    void wait();
    void post();
    
private:
    std::mutex m_;
    std::condition_variable cv_;
    int count_;
};

} // namespace osal
