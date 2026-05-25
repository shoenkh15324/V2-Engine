#pragma once
#include <mutex>

namespace osal{

class Mutex{
public:
    Mutex() = default;
    ~Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void lock();
    void unlock();
    
private:
    std::mutex m_;
};

} // namespace osal
