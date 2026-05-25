#include "mutex.hpp"

namespace osal{

void Mutex::lock(){
    m_.lock();
}

void Mutex::unlock(){
    m_.unlock();
}

} // namespace osal
