#pragma once
#include <mutex>

namespace osal{

class Mutex{
public:
    Mutex() = default;
    ~Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;
    void lock() noexcept;
    void unlock() noexcept;
    bool tryLock() noexcept;
    std::mutex& native() noexcept;
private:
    std::mutex m_;
};

} // namespace osal
