#include "mutex.hpp"

namespace osal{

void Mutex::lock() noexcept {
    m_.lock();
}

void Mutex::unlock() noexcept {
    m_.unlock();
}

bool Mutex::tryLock() noexcept {
    return m_.try_lock();
}

std::mutex& Mutex::native() noexcept {
    return m_;
}

} // namespace osal
