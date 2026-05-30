#include "thread.hpp"

namespace core::osal{

Thread::Thread(Callback cb, void* arg){
    if(cb){
        t_ = std::thread([cb = std::move(cb), arg]{
            cb(arg);
        });
    }
}

Thread::~Thread(){
    if(t_.joinable()){
        t_.join();
    }
}

Thread::Thread(Thread&& other) noexcept : t_(std::move(other.t_)){
}

Thread& Thread::operator=(Thread&& other) noexcept {
    if(this != &other){
        if(t_.joinable()){
            t_.join();
        }
        t_ = std::move(other.t_);
    }
    return *this;
}

void Thread::join(){
    if(t_.joinable()){
        t_.join();
    }
}

bool Thread::joinable() const {
    return t_.joinable();
}

} // namespace core::osal
