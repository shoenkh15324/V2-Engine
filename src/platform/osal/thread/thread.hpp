#pragma once
#include <thread>
#include <functional>

namespace osal{

class Thread{
public:
    using Callback = std::function<void(void*)>;
    Thread(Callback cb, void* arg);
    ~Thread();
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    void join();
    void detach();
    bool joinable() const;
    
private:
    std::thread t_;
    bool joined_ = false;
};

} // namespace osal
