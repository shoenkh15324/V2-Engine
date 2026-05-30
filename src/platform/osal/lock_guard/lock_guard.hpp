#pragma once

namespace osal{

template<typename T>
class LockGuard{
public:
    explicit LockGuard(T& lock) : lock_(lock){
        lock_.lock();
    }
    ~LockGuard(){
        lock_.unlock();
    }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&) = delete;
    LockGuard& operator=(LockGuard&&) = delete;

private:
    T& lock_;
};

} // namespace osal
