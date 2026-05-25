#include "thread.hpp"

namespace osal{

Thread::Thread(Callback cb, void* arg){
    t_ = std::thread([cb, arg](){
        if(cb){
            cb(arg);
        }
    });
}

Thread::~Thread(){
    if(t_.joinable()){
        t_.detach();
    }
}

void Thread::join(){
    if(t_.joinable()){
        t_.join();
        joined_ = true;
    }
}

void Thread::detach(){
    if(t_.joinable()){
        t_.detach();
    }
}

bool Thread::joinable() const {
    return t_.joinable();
}

} // namespace osal
