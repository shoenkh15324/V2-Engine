#pragma once
#include <cstdint>
#if defined(_WIN32)
    #include <Windows.h>
#elif defined(__linux__)
    #include <sys/timerfd.h>
    #include <unistd.h>
#endif

namespace core::osal{

class Timer{
public:
    Timer() = default;
    ~Timer();
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    int open(int periodMs);
    int close();
    intptr_t getHandle() const;

private:
#if defined(__linux__)
    int fd_ = -1;
#elif defined(_WIN32)
    HANDLE h_ = nullptr;
#endif
};

} // namespace core::osal
