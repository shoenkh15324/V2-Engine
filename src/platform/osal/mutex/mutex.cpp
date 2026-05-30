#include "mutex.hpp"

namespace osal{

void Mutex::lock(){
    m_.lock();
}

void Mutex::unlock(){
    m_.unlock();
}

bool Mutex::tryLock(){
    return m_.try_lock();
}

std::mutex& Mutex::native(){
    return m_;
}

} // namespace osal
