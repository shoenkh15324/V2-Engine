#pragma once
#include "core/common/util/platform_config.h"
#include <cstdint>

#if V2_PLATFORM_LINUX
#include <sys/epoll.h>

class Epoll{
public:
    Epoll();
    ~Epoll();

    Epoll(const Epoll&) = delete;
    Epoll& operator=(const Epoll&) = delete;
    Epoll(Epoll&&) noexcept;
    Epoll& operator=(Epoll&&) noexcept;

    int add(int fd, uint32_t events, void* ptr = nullptr);
    int mod(int fd, uint32_t events, void* ptr = nullptr);
    int del(int fd);
    int wait(epoll_event* events, int maxEvents, int timeoutMs = -1);
    int fd() const { return epollFd_; }

private:
    int epollFd_ = -1;
};

#endif
