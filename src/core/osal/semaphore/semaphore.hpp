#pragma once
#include <mutex>
#include <condition_variable>

namespace core::osal{

class Semaphore{
public:
    explicit Semaphore(int count = 0);
    ~Semaphore() = default;
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&) = delete;
    Semaphore& operator=(Semaphore&&) = delete;
    void wait();
    void post();
    bool tryWait();

private:
    std::mutex m_;
    std::condition_variable cv_;
    int count_;
};

} // namespace core::osal
