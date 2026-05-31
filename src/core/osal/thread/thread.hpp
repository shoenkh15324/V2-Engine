#pragma once
#include <thread>
#include <functional>

class Thread{
public:
    using Callback = std::function<void(void*)>;
    Thread(Callback cb, void* arg);
    ~Thread();
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;
    void join();    
    bool joinable() const;
    
private:
    std::thread t_;
};
